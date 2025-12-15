/*
* File: contree.hpp
* Project: blok
* Author: Collin
* Created on: 12/13/2025
*/

#ifndef CONTREE_HPP
#define CONTREE_HPP

#include <cstdint>
#include <vector>

#include "vec3.hpp"

namespace blok {

struct alignas(32) ContreeNode {
    uint64_t childMask;      // Bitmask of which children exist (0-63)
    uint32_t firstChild;     // Index to first child in compact array
    uint32_t materialId;
    float occupancy;
    uint32_t mortonPrefix;
    uint32_t childCount;     // Number of children (popcount of childMask)
    uint32_t reserved;
};
static_assert(sizeof(ContreeNode) == 32, "ContreeNode MUST be 32 bytes");

class Contree {
public:
    std::vector<ContreeNode> nodes;

    uint32_t  rootIndex;
    uint32_t  maxDepth;    // Number of 64-tree levels
    glm::vec3 origin;      // World space position of (0,0,0) corner
    float     voxelSize;   // World units per leaf voxel
    uint32_t  resolution;  // Voxels per axis <- this MUST be a power of 4

    Contree(uint32_t resolution, const glm::vec3& origin, float voxelSize);
    void clear();

    void insertVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t materialId, float density = 1.0f);

    [[nodiscard]]
    const ContreeNode* findLeaf(uint32_t x, uint32_t y, uint32_t z) const;

    [[nodiscard]]
    size_t nodeCount() const { return nodes.size(); }

    [[nodiscard]]
    size_t memoryUsage() const { return nodes.size() * sizeof(ContreeNode); }

    // Compacts the tree after batch insertions for optimal GPU upload
    // Call this after all insertions are complete
    void compact();

    [[nodiscard]]
    bool isCompacted() const { return compacted; }

    // GPU-compatible child offset calculation (popcount of bits below childIdx)
    static uint32_t childOffset(uint64_t childMask, uint32_t childIdx);

private:
    bool compacted;

    // Used during construction (pre-compaction)
    struct BuildNode {
        uint64_t childMask;
        uint32_t children[64];  // Direct child indices
        uint32_t materialId;
        float occupancy;
        uint32_t mortonPrefix;
    };
    std::vector<BuildNode> buildNodes;

    uint32_t ensureChild(uint32_t nodeIndex, uint32_t childIdx);

    static uint32_t calculateDepth(uint32_t resolution);
    static uint32_t popcount64(uint64_t x);
};

void buildContreeFromDense(
    const float* density,
    const uint32_t* materials,
    uint32_t resolution,
    const glm::vec3& origin,
    float voxelSize,
    Contree& contree
);

// Utility for GPU-side traversal (same logic as Contree::childOffset)
inline uint32_t contreeChildOffset(uint64_t childMask, uint32_t childIdx) {
    return Contree::childOffset(childMask, childIdx);
}

}

#endif //CONTREE_HPP