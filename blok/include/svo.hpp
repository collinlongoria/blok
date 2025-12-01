#ifndef SVO_HPP
#define SVO_HPP
#include <cassert>
#include <cstdint>
#include <vector>
#include <glm.hpp>

#include "morton.hpp"

namespace blok {

// unsigned 32 bit max
// means 'no children'
static constexpr uint32_t INVALID_NODE_INDEX = 0xFFFFFFFFu;

// std430 friendly
struct alignas(16) SvoNode {
    uint32_t childMask; // bits 0-7 tell which children are empty
    uint32_t firstChild; // index of first child in nodes[], or INVALID
    uint32_t color; // packed RGBA8, TODO: switch to material ID
    float    occupancy; // 0 = empty, >0 = filled
};

struct SvoTree {
    std::vector<SvoNode> nodes;

    uint32_t rootIndex;
    uint32_t maxDepth; // leaf level depth; 2^maxDepth cells per axis
    glm::vec3 origin; // world space position of voxel
    float voxelSize; // world units per leaf voxel

    SvoTree(uint32_t maxDepth, const glm::vec3& origin, float voxelSize);
    void clear(); // clears to the single empty root

    // insert a single filled voxel
    void insertVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t color, float density = 1.0f);

    // find leaf node for the given voxel coordinate (if it exists and non-empty)
    [[nodiscard]] const SvoNode* findLeaf(uint32_t x, uint32_t y, uint32_t z) const;
};

}

#endif