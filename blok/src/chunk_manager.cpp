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

// Number of subdivisions per axis
// MUST be a power of two
// TODO in a way, this is the beginning outline for LODs, lower numbers would mean coarser traversals
static constexpr uint32_t SUB_CHUNK_DIVISIONS = 8;

ChunkManager::ChunkManager(uint32_t C_, float voxelSize_)
    : C(C_), voxelSize(voxelSize_) {}

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
    auto* ch = new Chunk(cc.x, cc.y, cc.z, C, C, origin, voxelSize);

    chunks[cc] = ch;
    return ch;
}

void ChunkManager::setVoxel(const glm::vec3& worldPos, uint32_t materialId, float density) {
    glm::ivec3 gv = worldToGlobalVoxel(worldPos);
    ChunkCoord cc = globalVoxelToChunk(gv);
    glm::ivec3 lv = globalVoxelToLocal(gv, cc);

    Chunk* ch = getOrCreateChunk(cc);

    size_t idx = localIndex(lv.x, lv.y, lv.z);
    ch->density[idx] = density;
    ch->materialIds[idx] = materialId;

    ch->dirty = true;
}

void ChunkManager::setVoxel(const glm::vec3& worldPos, uint8_t r, uint8_t g, uint8_t b, float density) {
    uint32_t materialId;
    if (materialLib) {
        materialId = materialLib->getOrCreateFromColor(r, g, b);
    } else {
        // pack color directly as materialId
        materialId = (static_cast<uint32_t>(r) << 16) |
                     (static_cast<uint32_t>(g) << 8) |
                     (static_cast<uint32_t>(b) << 0);
    }
    setVoxelMaterial(worldPos, materialId, density);
}

// helper to rebuild svo
// TODO: this is a testing version. update to make less naive.
void buildSv64FromDensity(Chunk* ch, uint32_t C) {
    ch->sv64.clear();

    for (uint32_t z = 0; z < C; ++z)
        for (uint32_t y = 0; y < C; ++y)
            for (uint32_t x = 0; x < C; ++x) {
                size_t idx = x + y*C + z*C*C;
                float d = ch->density[idx];
                if (d > 0.0f) {
                    uint32_t materialId = ch->materialIds[idx];
                    ch->sv64.insertVoxel(x, y, z, materialId, d);
                }
            }
    ch->sv64.compact();
}

void rebuildDirtyChunks(ChunkManager& mgr, int maxPerFrame) {
    int count = 0;
    for (auto& kv : mgr.chunks) {
        Chunk* ch = kv.second;
        if (!ch->dirty) continue;
        if (count >= maxPerFrame) break;

        // rebuild
        buildSv64FromDensity(ch, mgr.C);

        ch->dirty = false;
        count++;

        // TODO: could benefit from better logging system
        std::cout << "Chunk (" << ch->cx << "," << ch->cy << "," << ch->cz
          << ") - SV64 node count: " << ch->sv64.nodes.size() << "\n";
    }
}


// Check if a sub-region of the SV64 contains any geometry
// by checking the childMask bits along the path to that sub-region
static bool subChunkHasGeometry(
    const std::vector<Sv64Node>& nodes,
    uint32_t subX, uint32_t subY, uint32_t subZ,
    uint32_t subDivisions,
    uint32_t maxDepth
) {
    if (nodes.empty()) return false;

    // Calculate how many levels of the SV64 we need to descend to reach sub-chunk level
    uint32_t subChunkDepth = 0;
    while ((1u << (subChunkDepth * 2)) < subDivisions) subChunkDepth++;

    // Traverse down to sub-chunk root
    uint32_t nodeIndex = 0;  // Start at root

    for (uint32_t level = 0; level < subChunkDepth && level < maxDepth; level++) {
        const Sv64Node& node = nodes[nodeIndex];

        // Calculate which child (0-63) this sub-chunk falls into at this level
        // SV64 uses 4x4x4 children per level
        uint32_t levelDivisions = 1u << ((level + 1) * 2);  // 4, 16, 64, ...
        uint32_t cellSize = subDivisions / levelDivisions;
        if (cellSize == 0) cellSize = 1;

        uint32_t childX = (subX / cellSize) & 0x3;
        uint32_t childY = (subY / cellSize) & 0x3;
        uint32_t childZ = (subZ / cellSize) & 0x3;
        uint32_t childIdx = childX | (childY << 2) | (childZ << 4);

        // Check if this child has children
        if ((node.childMask & (1ull << childIdx)) == 0) {
            return false;  // No geometry in this sub-chunk
        }

        if (node.firstChild == 0xFFFFFFFFu) {
            return false;  // Invalid child pointer
        }

        // Use popcount to find the actual child index in the compact array
        uint32_t offset = Sv64::childOffset(node.childMask, childIdx);
        nodeIndex = node.firstChild + offset;

        if (nodeIndex >= nodes.size()) {
            return false;  // Out of bounds
        }
    }

    // At sub-chunk root - check if it has any children (geometry)
    const Sv64Node& subRoot = nodes[nodeIndex];
    return subRoot.childMask != 0 || subRoot.occupancy > 0.0f;
}

// Find the node index for a sub-chunk's root
static uint32_t findSubChunkRootNode(
    const std::vector<Sv64Node>& nodes,
    uint32_t subX, uint32_t subY, uint32_t subZ,
    uint32_t subDivisions,
    uint32_t maxDepth
) {
    if (nodes.empty()) return 0;

    uint32_t subChunkDepth = 0;
    while ((1u << (subChunkDepth * 2)) < subDivisions) subChunkDepth++;

    uint32_t nodeIndex = 0;

    for (uint32_t level = 0; level < subChunkDepth && level < maxDepth; level++) {
        const Sv64Node& node = nodes[nodeIndex];

        uint32_t levelDivisions = 1u << ((level + 1) * 2);
        uint32_t cellSize = subDivisions / levelDivisions;
        if (cellSize == 0) cellSize = 1;

        uint32_t childX = (subX / cellSize) & 0x3;
        uint32_t childY = (subY / cellSize) & 0x3;
        uint32_t childZ = (subZ / cellSize) & 0x3;
        uint32_t childIdx = childX | (childY << 2) | (childZ << 4);

        if ((node.childMask & (1ull << childIdx)) == 0) {
            return nodeIndex;  // Can't go deeper, return current
        }

        if (node.firstChild == 0xFFFFFFFFu) {
            return nodeIndex;  // Can't go deeper, return current
        }

        uint32_t offset = Sv64::childOffset(node.childMask, childIdx);
        nodeIndex = node.firstChild + offset;

        if (nodeIndex >= nodes.size()) {
            return 0;
        }
    }

    return nodeIndex;
}

void packChunksToGpuSv64(const ChunkManager& mgr, WorldSv64Gpu& gpuWorld) {
    gpuWorld.globalNodes.clear();
    gpuWorld.globalSubChunks.clear();

    gpuWorld.globalNodes.reserve(1024);
    gpuWorld.globalSubChunks.reserve(mgr.chunks.size() * SUB_CHUNK_DIVISIONS * SUB_CHUNK_DIVISIONS);

    uint32_t nodeOffset = 0;
    uint32_t totalSubChunks = 0;
    uint32_t emptySubChunks = 0;

    // Calculate sub-chunk depth (how many SV64 levels to skip)
    uint32_t subChunkDepth = 0;
    while ((1u << (subChunkDepth * 2)) < SUB_CHUNK_DIVISIONS) subChunkDepth++;

    for (auto& kv : mgr.chunks) {
        const Chunk* ch = kv.second;
        const auto& nodes = ch->sv64.nodes;
        if (nodes.empty()) continue;

        // Chunk's world-space origin
        glm::vec3 chunkOrigin(
            static_cast<float>(ch->cx * static_cast<int32_t>(mgr.C)),
            static_cast<float>(ch->cy * static_cast<int32_t>(mgr.C)),
            static_cast<float>(ch->cz * static_cast<int32_t>(mgr.C))
        );
        chunkOrigin *= mgr.voxelSize;

        float chunkWorldSize = static_cast<float>(mgr.C) * mgr.voxelSize;
        float subChunkWorldSize = chunkWorldSize / static_cast<float>(SUB_CHUNK_DIVISIONS);

        uint32_t maxDepth = ch->sv64.maxDepth;

        // Iterate over all sub-chunk positions
        for (uint32_t sz = 0; sz < SUB_CHUNK_DIVISIONS; sz++) {
            for (uint32_t sy = 0; sy < SUB_CHUNK_DIVISIONS; sy++) {
                for (uint32_t sx = 0; sx < SUB_CHUNK_DIVISIONS; sx++) {
                    totalSubChunks++;

                    // Check if this sub-chunk has any geometry
                    if (!subChunkHasGeometry(nodes, sx, sy, sz, SUB_CHUNK_DIVISIONS, maxDepth)) {
                        emptySubChunks++;
                        continue;  // Skip empty sub-chunks
                    }

                    // Find the root node for this sub-chunk
                    uint32_t subRootNode = findSubChunkRootNode(
                        nodes, sx, sy, sz, SUB_CHUNK_DIVISIONS, maxDepth
                    );

                    // Calculate world-space bounds
                    glm::vec3 subMin = chunkOrigin + glm::vec3(
                        static_cast<float>(sx) * subChunkWorldSize,
                        static_cast<float>(sy) * subChunkWorldSize,
                        static_cast<float>(sz) * subChunkWorldSize
                    );
                    glm::vec3 subMax = subMin + glm::vec3(subChunkWorldSize);

                    // Create sub-chunk entry
                    SubChunkGpu sub{};
                    sub.nodeOffset = nodeOffset;
                    sub.rootNodeIndex = subRootNode;
                    sub.nodeCount = static_cast<uint32_t>(nodes.size());
                    sub.startDepth = subChunkDepth;
                    sub.worldMin = subMin;
                    sub.subChunkSize = subChunkWorldSize;
                    sub.worldMax = subMax;

                    gpuWorld.globalSubChunks.push_back(sub);
                }
            }
        }

        // Append this chunk's nodes to global array
        gpuWorld.globalNodes.insert(gpuWorld.globalNodes.end(), nodes.begin(), nodes.end());
        nodeOffset += static_cast<uint32_t>(nodes.size());
    }

    std::cout << "Sub-chunk packing: " << gpuWorld.globalSubChunks.size()
              << " active sub-chunks out of " << totalSubChunks
              << " total (" << emptySubChunks << " culled)\n";
    std::cout << "Total SV64 nodes: " << gpuWorld.globalNodes.size() << "\n";
}

void ChunkManager::setVoxelMaterial(const glm::vec3& worldPos, uint32_t materialId, float density) {
    glm::ivec3 gv = worldToGlobalVoxel(worldPos);
    ChunkCoord cc = globalVoxelToChunk(gv);
    glm::ivec3 lv = globalVoxelToLocal(gv, cc);

    Chunk* ch = getOrCreateChunk(cc);

    size_t idx = localIndex(lv.x, lv.y, lv.z);
    ch->density[idx] = density;
    ch->materialIds[idx] = materialId;

    ch->dirty = true;
}

uint32_t ChunkManager::getVoxelMaterial(const glm::vec3& worldPos) const {
    glm::ivec3 gv = worldToGlobalVoxel(worldPos);
    ChunkCoord cc = globalVoxelToChunk(gv);

    auto it = chunks.find(cc);
    if (it == chunks.end()) {
        return 0; // Not found
    }

    glm::ivec3 lv = globalVoxelToLocal(gv, cc);
    size_t idx = localIndex(lv.x, lv.y, lv.z);

    const Chunk* ch = it->second;
    if (ch->density[idx] <= 0.0f) {
        return 0; // Empty
    }

    return ch->materialIds[idx];
}

}