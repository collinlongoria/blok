/*
* File: chunk_manager.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include "chunk_manager.hpp"

#include <cmath>
#include <iostream>

namespace blok {

ChunkManager::ChunkManager(uint32_t C_, float voxelSize_)
    : C(C_), voxelSize(voxelSize_) {
    maxDepth = 0;
    // require C = 2^maxDepth
    while ((1u << maxDepth) < C)
        maxDepth++;
}

ChunkManager::~ChunkManager() {
    for (auto& kv : chunks) delete kv.second;
}

glm::ivec3 ChunkManager::worldToGlobalVoxel(const glm::vec3 &p) const {
    // Map world position directly to voxel coordinates (1:1 mapping)
    // voxelSize only affects the visual size, not the coordinate mapping
    return {
        int(std::floor(p.x)),
        int(std::floor(p.y)),
        int(std::floor(p.z))
    };
}

ChunkCoord ChunkManager::globalVoxelToChunk(const glm::ivec3 &gv) const {
    return {
        gv.x >= 0 ? gv.x / static_cast<int32_t>(C) : (gv.x - static_cast<int32_t>(C) + 1) / static_cast<int32_t>(C),
        gv.y >= 0 ? gv.y / static_cast<int32_t>(C) : (gv.y - static_cast<int32_t>(C) + 1) / static_cast<int32_t>(C),
        gv.z >= 0 ? gv.z / static_cast<int32_t>(C) : (gv.z - static_cast<int32_t>(C) + 1) / static_cast<int32_t>(C)
    };
}

glm::ivec3 ChunkManager::globalVoxelToLocal(const glm::ivec3 &gv, const ChunkCoord &cc) const {
    return {
        gv.x - cc.x * static_cast<int32_t>(C),
        gv.y - cc.y * static_cast<int32_t>(C),
        gv.z - cc.z * static_cast<int32_t>(C)
    };
}

size_t ChunkManager::localIndex(int lx, int ly, int lz) const {
    return static_cast<size_t>(lx) + static_cast<size_t>(ly) * C + static_cast<size_t>(lz) * C * C;
}

Chunk *ChunkManager::getOrCreateChunk(const ChunkCoord &cc) {
    auto it = chunks.find(cc);
    if (it != chunks.end())
        return it->second;

    glm::vec3 origin = glm::vec3(
        static_cast<float>(cc.x * static_cast<int32_t>(C)) * voxelSize,
        static_cast<float>(cc.y * static_cast<int32_t>(C)) * voxelSize,
        static_cast<float>(cc.z * static_cast<int32_t>(C)) * voxelSize
    );
    auto* ch = new Chunk(cc.x, cc.y, cc.z, C, maxDepth, origin, voxelSize);

    chunks[cc] = ch;
    return ch;
}

void ChunkManager::setBrushColor(uint8_t r, uint8_t g, uint8_t b) {
    brushR = r;
    brushG = g;
    brushB = b;
}

uint32_t ChunkManager::getBrushColorPacked() const {
    return (static_cast<uint32_t>(brushR) << 16) |
           (static_cast<uint32_t>(brushG) << 8) |
           (static_cast<uint32_t>(brushB) << 0);
}

void ChunkManager::setVoxel(const glm::vec3& worldPos, float density) {
    setVoxel(worldPos, brushR, brushG, brushB, density);
}

void ChunkManager::setVoxel(const glm::vec3& worldPos, uint8_t r, uint8_t g, uint8_t b, float density) {
    glm::ivec3 gv = worldToGlobalVoxel(worldPos);
    ChunkCoord cc = globalVoxelToChunk(gv);
    glm::ivec3 lv = globalVoxelToLocal(gv, cc);

    Chunk* ch = getOrCreateChunk(cc);

    size_t idx = localIndex(lv.x, lv.y, lv.z);
    ch->density[idx] = density;

    // Store the color so it survives rebuild
    uint32_t color = (static_cast<uint32_t>(r) << 16) |
                     (static_cast<uint32_t>(g) << 8) |
                     (static_cast<uint32_t>(b) << 0);
    ch->colors[idx] = color;

    ch->dirty = true;
}

// helper to rebuild svo
// TODO: this is a testing version. update to make less naive.
void buildSvoFromDensity(Chunk* ch, uint32_t C) {
    ch->svo.clear();

    for (uint32_t z = 0; z < C; ++z)
        for (uint32_t y = 0; y < C; ++y)
            for (uint32_t x = 0; x < C; ++x) {
                size_t idx = x + y*C + z*C*C;
                float d = ch->density[idx];
                if (d > 0.0f) {
                    uint32_t color = ch->colors[idx];  // Use stored color
                    ch->svo.insertVoxel(x, y, z, color, d);
                }
            }
}

void rebuildDirtyChunks(ChunkManager& mgr, int maxPerFrame) {
    int count = 0;
    for (auto& kv : mgr.chunks) {
        Chunk* ch = kv.second;
        if (!ch->dirty) continue;
        if (count >= maxPerFrame) break;

        ch->svo.clear();

        // rebuild
        buildSvoFromDensity(ch, mgr.C);

        ch->dirty = false;
        count++;

        // TODO: debug print. establish macro or delete
        std::cout << "Chunk (" << ch->cx << "," << ch->cy << "," << ch->cz
          << ") - SVO node count: " << ch->svo.nodes.size() << "\n";
    }
}

void packChunksToGpuSvo(const ChunkManager& mgr, WorldSvoGpu& gpuWorld) {
    gpuWorld.globalNodes.clear();
    gpuWorld.globalChunks.clear();

    gpuWorld.globalNodes.reserve(1024);
    gpuWorld.globalChunks.reserve(mgr.chunks.size());

    uint32_t nodeOffset = 0;
    uint32_t chunkIndex = 0;

    for (auto& kv : mgr.chunks) {
        const Chunk* ch = kv.second;
        const auto& nodes = ch->svo.nodes;
        if (nodes.empty()) continue;

        ChunkGpu meta{};
        meta.nodeOffset = nodeOffset;
        meta.nodeCount = static_cast<uint32_t>(nodes.size());

        glm::vec3 chunkOrigin(
            static_cast<float>(ch->cx * static_cast<int32_t>(mgr.C)),
            static_cast<float>(ch->cy * static_cast<int32_t>(mgr.C)),
            static_cast<float>(ch->cz * static_cast<int32_t>(mgr.C))
        );
        auto C = static_cast<float>(mgr.C);
        float vs = mgr.voxelSize;

        meta.worldMin = chunkOrigin * mgr.voxelSize;
        meta.worldMax = (chunkOrigin + glm::vec3(mgr.C)) * mgr.voxelSize;

        gpuWorld.globalChunks.push_back(meta);

        // append nodes
        gpuWorld.globalNodes.insert(gpuWorld.globalNodes.end(), nodes.begin(), nodes.end());
        nodeOffset += meta.nodeCount;
        chunkIndex++;
    }
}

}
