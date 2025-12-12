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
static constexpr uint32_t SUB_CHUNK_DIVISIONS = 4;

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
void buildSvoFromDensity(Chunk* ch, uint32_t C) {
    ch->svo.clear();

    for (uint32_t z = 0; z < C; ++z)
        for (uint32_t y = 0; y < C; ++y)
            for (uint32_t x = 0; x < C; ++x) {
                size_t idx = x + y*C + z*C*C;
                float d = ch->density[idx];
                if (d > 0.0f) {
                    uint32_t materialId = ch->materialIds[idx];
                    ch->svo.insertVoxel(x, y, z, materialId, d);
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

// Check if a sub-region of the SVO contains any geometry
// by checking the childMask bits along the path to that sub-region
static bool subChunkHasGeometry(
    const std::vector<SvoNode>& nodes,
    uint32_t subX, uint32_t subY, uint32_t subZ,
    uint32_t subDivisions,
    uint32_t maxDepth
) {
    if (nodes.empty()) return false;

    // Calculate how many levels of the SVO we need to descend to reach sub-chunk level
    // If chunk is 128³ (maxDepth=7) and subDivisions=4:
    //   subChunkSize = 128/4 = 32 voxels = 2^5, so we descend 2 levels (7-5=2)
    uint32_t subChunkVoxels = (1u << maxDepth) / subDivisions;
    uint32_t subChunkDepth = 0;
    while ((1u << subChunkDepth) < subDivisions) subChunkDepth++;

    // Traverse down to sub-chunk root
    uint32_t nodeIndex = 0;  // Start at root

    for (uint32_t level = 0; level < subChunkDepth; level++) {
        const SvoNode& node = nodes[nodeIndex];

        // Calculate which octant this sub-chunk falls into at this level
        uint32_t levelDivisions = 1u << (level + 1);
        uint32_t cellSize = subDivisions / levelDivisions;

        uint32_t octX = (subX / cellSize) & 1;
        uint32_t octY = (subY / cellSize) & 1;
        uint32_t octZ = (subZ / cellSize) & 1;
        uint32_t octant = octX | (octY << 1) | (octZ << 2);

        // Check if this octant has children
        if ((node.childMask & (1u << octant)) == 0) {
            return false;  // No geometry in this sub-chunk
        }

        if (node.firstChild == 0xFFFFFFFFu) {
            return false;  // Invalid child pointer
        }

        nodeIndex = node.firstChild + octant;

        if (nodeIndex >= nodes.size()) {
            return false;  // Out of bounds
        }
    }

    // At sub-chunk root - check if it has any children (geometry)
    const SvoNode& subRoot = nodes[nodeIndex];
    return subRoot.childMask != 0 || subRoot.occupancy > 0.0f;
}

// Find the node index for a sub-chunk's root
static uint32_t findSubChunkRootNode(
    const std::vector<SvoNode>& nodes,
    uint32_t subX, uint32_t subY, uint32_t subZ,
    uint32_t subDivisions,
    uint32_t maxDepth
) {
    if (nodes.empty()) return 0;

    uint32_t subChunkDepth = 0;
    while ((1u << subChunkDepth) < subDivisions) subChunkDepth++;

    uint32_t nodeIndex = 0;

    for (uint32_t level = 0; level < subChunkDepth; level++) {
        const SvoNode& node = nodes[nodeIndex];

        uint32_t levelDivisions = 1u << (level + 1);
        uint32_t cellSize = subDivisions / levelDivisions;

        uint32_t octX = (subX / cellSize) & 1;
        uint32_t octY = (subY / cellSize) & 1;
        uint32_t octZ = (subZ / cellSize) & 1;
        uint32_t octant = octX | (octY << 1) | (octZ << 2);

        if (node.firstChild == 0xFFFFFFFFu) {
            return nodeIndex;  // Can't go deeper, return current
        }

        nodeIndex = node.firstChild + octant;

        if (nodeIndex >= nodes.size()) {
            return 0;
        }
    }

    return nodeIndex;
}

void packChunksToGpuSvo(const ChunkManager& mgr, WorldSvoGpu& gpuWorld) {
    gpuWorld.globalNodes.clear();
    gpuWorld.globalSubChunks.clear();

    gpuWorld.globalNodes.reserve(1024);
    gpuWorld.globalSubChunks.reserve(mgr.chunks.size() * SUB_CHUNK_DIVISIONS * SUB_CHUNK_DIVISIONS);

    uint32_t nodeOffset = 0;
    uint32_t totalSubChunks = 0;
    uint32_t emptySubChunks = 0;

    // Calculate sub-chunk depth (how many SVO levels to skip)
    uint32_t subChunkDepth = 0;
    while ((1u << subChunkDepth) < SUB_CHUNK_DIVISIONS) subChunkDepth++;

    for (auto& kv : mgr.chunks) {
        const Chunk* ch = kv.second;
        const auto& nodes = ch->svo.nodes;
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

        // Iterate over all sub-chunk positions
        for (uint32_t sz = 0; sz < SUB_CHUNK_DIVISIONS; sz++) {
            for (uint32_t sy = 0; sy < SUB_CHUNK_DIVISIONS; sy++) {
                for (uint32_t sx = 0; sx < SUB_CHUNK_DIVISIONS; sx++) {
                    totalSubChunks++;

                    // Check if this sub-chunk has any geometry
                    if (!subChunkHasGeometry(nodes, sx, sy, sz, SUB_CHUNK_DIVISIONS, mgr.maxDepth)) {
                        emptySubChunks++;
                        continue;  // Skip empty sub-chunks
                    }

                    // Find the root node for this sub-chunk
                    uint32_t subRootNode = findSubChunkRootNode(
                        nodes, sx, sy, sz, SUB_CHUNK_DIVISIONS, mgr.maxDepth
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
    std::cout << "Total SVO nodes: " << gpuWorld.globalNodes.size() << "\n";
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
