/*
* File: ${FILE}.${EXTENSION}
* Project: ${PROJECT}
* Author: Collin
* Created on: 12/13/2025
*/

#include "contree.hpp"

#include "morton.hpp"

namespace blok {

static constexpr uint32_t INVALID_NODE_INDEX = 0xFFFFFFFFu;

static ContreeNode makeEmptyContreeNode() {
    ContreeNode n{};
    n.childMask = 0ull;
    n.firstChild = INVALID_NODE_INDEX;
    n.materialId = 0u;
    n.occupancy = 0.0f;
    n.mortonPrefix = 0u;
    n.reserved[0] = 0u;
    n.reserved[1] = 0u;
    return n;
}

uint32_t Contree::calculateDepth(uint32_t resolution) {
    if (resolution == 0) return 0;

    uint32_t depth = 0;
    uint32_t size = 1;
    while (size < resolution) {
        size *= 4;
        depth++;
    }
    return depth;
}

Contree::Contree(uint32_t resolution_, const glm::vec3& origin_, float voxelSize_)
    : rootIndex(0), maxDepth(calculateDepth(resolution_)), origin(origin_), voxelSize(voxelSize_), resolution(resolution_) {
    nodes.reserve(1024);
    nodes.clear();
    nodes.push_back(makeEmptyContreeNode());
}

void Contree::clear() {
    nodes.clear();
    nodes.push_back(makeEmptyContreeNode());
    rootIndex = 0;
}

uint32_t Contree::ensureChildren(uint32_t nodeIndex) {
    // Check if children already exist
    if (nodes[nodeIndex].firstChild != INVALID_NODE_INDEX) {
        return nodes[nodeIndex].firstChild;
    }

    const uint32_t firstChild = static_cast<uint32_t>(nodes.size());

    // Reserve space to avoid reallocations
    // 64 children per node
    if (nodes.capacity() < nodes.size() + 64) {
        nodes.reserve(nodes.capacity() * 2 + 64);
    }

    // Allocate 64 children
    nodes.resize(nodes.size() + 64);

    // Initialize all children as empty
    for (uint32_t i = 0; i < 64; ++i) {
        nodes[firstChild + i] = makeEmptyContreeNode();
    }

    // Set parent's firstChild pointer
    nodes[nodeIndex].firstChild = firstChild;

    return firstChild;
}

void Contree::insertVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t materialId, float density) {
    if (density <= 0.0f) return;

    // Bounds check
    if (x >= resolution || y >= resolution || z >= resolution) return;

    // Compute Morton code for this voxel
    uint64_t mortonCode = morton3d::encodeUnsigned(x, y, z);

    uint32_t nodeIndex = rootIndex;

    // Track path for childMask propagation
    uint32_t pathNodeIndices[16];  // Max 16 levels for 64-tree (huge worlds)
    uint32_t pathChildIndices[16];

    // Descend through the tree
    for (uint32_t level = 0; level < maxDepth; ++level) {
        pathNodeIndices[level] = nodeIndex;

        // Extract child index for this level (6 bits)
        uint32_t childIdx = morton3d::childIndex64FromCode(mortonCode, maxDepth, level);
        pathChildIndices[level] = childIdx;

        // Ensure children exist
        uint32_t firstChild = ensureChildren(nodeIndex);
        nodeIndex = firstChild + childIdx;
    }

    // nodeIndex is now the leaf node
    ContreeNode& leaf = nodes[nodeIndex];
    leaf.materialId = materialId;
    leaf.occupancy = density;
    leaf.mortonPrefix = static_cast<uint32_t>(mortonCode & 0xFFFFFFFFull);

    // Propagate childMask up the path
    for (int level = static_cast<int>(maxDepth) - 1; level >= 0; --level) {
        uint32_t parentIndex = pathNodeIndices[level];
        uint32_t childIdx = pathChildIndices[level];

        nodes[parentIndex].childMask |= (1ull << childIdx);
    }
}

const ContreeNode *Contree::findLeaf(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= resolution || y >= resolution || z >= resolution) return nullptr;

    uint64_t mortonCode = morton3d::encodeUnsigned(x, y, z);
    uint32_t nodeIndex = rootIndex;

    for (uint32_t level = 0; level < maxDepth; ++level) {
        uint32_t childIdx = morton3d::childIndex64FromCode(mortonCode, maxDepth, level);

        const ContreeNode& node = nodes[nodeIndex];

        // Check if this child exists
        if ((node.childMask & (1ull << childIdx)) == 0ull) {
            return nullptr;
        }

        if (node.firstChild == INVALID_NODE_INDEX) {
            return nullptr;
        }

        nodeIndex = node.firstChild + childIdx;
    }

    const ContreeNode& leaf = nodes[nodeIndex];
    if (leaf.occupancy <= 0.0f) return nullptr;

    return &leaf;
}

void buildContreeFromDense(const float* density, const uint32_t* materials, uint32_t resolution, const glm::vec3& origin, float voxelSize, Contree& contree) {
    contree = Contree(resolution, origin, voxelSize);

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                size_t idx = x + y * resolution + z * resolution * resolution;
                float d = density[idx];
                if (d > 0.0f) {
                    uint32_t mat = materials[idx];
                    contree.insertVoxel(x, y, z, mat, d);
                }
            }
        }
    }
}
}
