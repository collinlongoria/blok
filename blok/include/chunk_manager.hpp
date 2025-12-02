/*
* File: chunk_manager.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef CHUNK_MANAGER_HPP
#define CHUNK_MANAGER_HPP
#include <unordered_map>

#include "chunk.hpp"
#include "resources.hpp"

namespace blok {

class ChunkManager {
public:
    uint32_t C; // voxels per chunk edge
    float voxelSize; // world units per voxel
    uint32_t maxDepth;

    std::unordered_map<ChunkCoord, Chunk*, ChunkCoordHash> chunks;

    uint8_t brushR = 255;
    uint8_t brushG = 255;
    uint8_t brushB = 255;

public:
    ChunkManager(uint32_t C, float voxelSize);
    ~ChunkManager();

    glm::ivec3 worldToGlobalVoxel(const glm::vec3& p) const;
    ChunkCoord globalVoxelToChunk(const glm::ivec3& gv) const;
    glm::ivec3 globalVoxelToLocal(const glm::ivec3& gv, const ChunkCoord& cc) const;
    size_t localIndex(int lx, int ly, int lz) const;

    Chunk* getOrCreateChunk(const ChunkCoord& cc);

    void setBrushColor(uint8_t r, uint8_t g, uint8_t b);
    uint32_t getBrushColorPacked() const;

    void setVoxel(const glm::vec3& worldPos, float density = 1.0f);
    void setVoxel(const glm::vec3& worldPos, uint8_t r, uint8_t g, uint8_t b, float density = 1.0f);

};

void rebuildDirtyChunks(ChunkManager& mgr, int maxPerFrame);
void packChunksToGpuSvo(const ChunkManager& mgr, WorldSvoGpu& gpuWorld);

}

#endif