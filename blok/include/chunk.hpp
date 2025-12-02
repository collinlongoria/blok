/*
* File: chunk.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef CHUNK_HPP
#define CHUNK_HPP
#include <vector>
#include <vec3.hpp>

#include "svo.hpp"

namespace blok {
struct ChunkCoord {
    int32_t x, y, z;

    bool operator==(const ChunkCoord &o) const noexcept {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord &c) const noexcept {
        uint64_t h = 146527u;
        h = (h * 16777619ull) ^ static_cast<uint64_t>(c.x);
        h = (h * 16777619ull) ^ static_cast<uint64_t>(c.y);
        h = (h * 16777619ull) ^ static_cast<uint64_t>(c.z);
        return static_cast<size_t>(h);
    }
};

struct Chunk {
    int32_t cx, cy, cz; // chunk index
    std::vector<float> density; // size C*C*C
    std::vector<uint32_t> colors;
    bool dirty;
    SvoTree svo;

    Chunk(int32_t cx_, int32_t cy_, int32_t cz_, uint32_t C, uint32_t maxDepth, const glm::vec3& origin, float voxelSize)
        : cx(cx_), cy(cy_), cz(cz_), density(C*C*C, 0.0f), colors(C*C*C, 0xFFFFFFu), dirty(true), svo(maxDepth, origin, voxelSize) {}
};

}
#endif