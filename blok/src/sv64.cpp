/*
* File: sv64.cpp
* Project: blok
* Author: Collin
* Created on: 12/13/2025
*/

#include "sv64.hpp"

#include "morton.hpp"

namespace blok {

static constexpr uint32_t INVALID_NODE_INDEX = 0xFFFFFFFFu;

uint32_t Sv64::calculateDepth(uint32_t resolution) {
    if (resolution == 0) return 0;

    uint32_t depth = 0;
    uint32_t size = 1;
    while (size < resolution) {
        size *= 4;
        depth++;
    }
    return depth;
}

uint32_t Sv64::popcount64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0full;
    return static_cast<uint32_t>((x * 0x0101010101010101ull) >> 56);
}

uint32_t Sv64::childOffset(uint64_t childMask, uint32_t childIdx) {
    uint64_t mask = (1ull << childIdx) - 1ull;
    return popcount64(childMask & mask);
}

Sv64::Sv64(uint32_t resolution_, const glm::vec3& origin_, float voxelSize_)
    : rootIndex(0)
    , maxDepth(calculateDepth(resolution_))
    , origin(origin_)
    , voxelSize(voxelSize_)
    , resolution(resolution_)
    , compacted(false) {
    buildNodes.reserve(1024);
    buildNodes.clear();

    // Initialize root build node
    BuildNode root{};
    root.childMask = 0ull;
    for (uint32_t i = 0; i < 64; ++i) {
        root.children[i] = INVALID_NODE_INDEX;
    }
    root.materialId = 0u;
    root.occupancy = 0.0f;
    root.mortonPrefix = 0u;
    buildNodes.push_back(root);
}

void Sv64::clear() {
    buildNodes.clear();

    // Initialize root build node
    BuildNode root{};
    root.childMask = 0ull;
    for (uint32_t i = 0; i < 64; ++i) {
        root.children[i] = INVALID_NODE_INDEX;
    }
    root.materialId = 0u;
    root.occupancy = 0.0f;
    root.mortonPrefix = 0u;
    buildNodes.push_back(root);

    nodes.clear();
    rootIndex = 0;
    compacted = false;
}

uint32_t Sv64::ensureChild(uint32_t nodeIndex, uint32_t childIdx) {
    BuildNode& node = buildNodes[nodeIndex];

    // Check if child already exists
    if (node.children[childIdx] != INVALID_NODE_INDEX) {
        return node.children[childIdx];
    }

    // Allocate new child
    uint32_t newChildIndex = static_cast<uint32_t>(buildNodes.size());

    if (buildNodes.capacity() < buildNodes.size() + 1) {
        buildNodes.reserve(buildNodes.capacity() * 2 + 64);
    }

    // Create new empty build node
    BuildNode newNode{};
    newNode.childMask = 0ull;
    for (uint32_t i = 0; i < 64; ++i) {
        newNode.children[i] = INVALID_NODE_INDEX;
    }
    newNode.materialId = 0u;
    newNode.occupancy = 0.0f;
    newNode.mortonPrefix = 0u;
    buildNodes.push_back(newNode);

    // Update parent (note: push_back may invalidate references, so re-fetch)
    buildNodes[nodeIndex].children[childIdx] = newChildIndex;
    buildNodes[nodeIndex].childMask |= (1ull << childIdx);

    return newChildIndex;
}

void Sv64::insertVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t materialId, float density) {
    if (density <= 0.0f) return;
    if (x >= resolution || y >= resolution || z >= resolution) return;

    // Must not be compacted to insert
    if (compacted) {
        clear();  // Reset if trying to insert after compaction
    }

    uint64_t mortonCode = morton3d::encodeUnsigned(x, y, z);

    uint32_t nodeIndex = rootIndex;

    // Descend through the tree, creating nodes as needed
    for (uint32_t level = 0; level < maxDepth; ++level) {
        uint32_t childIdx = morton3d::childIndex64FromCode(mortonCode, maxDepth, level);
        nodeIndex = ensureChild(nodeIndex, childIdx);
    }

    // nodeIndex is now the leaf node
    BuildNode& leaf = buildNodes[nodeIndex];
    leaf.materialId = materialId;
    leaf.occupancy = density;
    leaf.mortonPrefix = static_cast<uint32_t>(mortonCode & 0xFFFFFFFFull);
}

void Sv64::compact() {
    if (compacted) return;
    if (buildNodes.empty()) return;

    // Build a mapping from old indices to new indices using BFS
    // This ensures parents come before children and siblings are contiguous
    std::vector<uint32_t> newIndices(buildNodes.size(), INVALID_NODE_INDEX);
    std::vector<uint32_t> bfsOrder;
    bfsOrder.reserve(buildNodes.size());

    // BFS traversal
    bfsOrder.push_back(rootIndex);
    size_t queuePos = 0;

    while (queuePos < bfsOrder.size()) {
        uint32_t oldIdx = bfsOrder[queuePos++];
        const BuildNode& bn = buildNodes[oldIdx];

        // Add children in order (this keeps siblings contiguous)
        for (uint32_t i = 0; i < 64; ++i) {
            if (bn.children[i] != INVALID_NODE_INDEX) {
                bfsOrder.push_back(bn.children[i]);
            }
        }
    }

    // Assign new indices based on BFS order
    for (uint32_t newIdx = 0; newIdx < bfsOrder.size(); ++newIdx) {
        newIndices[bfsOrder[newIdx]] = newIdx;
    }

    // Build the compacted nodes array
    nodes.clear();
    nodes.resize(buildNodes.size());

    for (uint32_t newIdx = 0; newIdx < bfsOrder.size(); ++newIdx) {
        uint32_t oldIdx = bfsOrder[newIdx];
        const BuildNode& bn = buildNodes[oldIdx];

        Sv64Node& cn = nodes[newIdx];
        cn.childMask = bn.childMask;
        cn.materialId = bn.materialId;
        cn.occupancy = bn.occupancy;
        cn.mortonPrefix = bn.mortonPrefix;
        cn.childCount = popcount64(bn.childMask);
        cn.reserved = 0;

        if (cn.childCount > 0) {
            // Find the first child's new index
            // Because of BFS order, children are contiguous
            for (uint32_t i = 0; i < 64; ++i) {
                if (bn.children[i] != INVALID_NODE_INDEX) {
                    cn.firstChild = newIndices[bn.children[i]];
                    break;
                }
            }
        } else {
            cn.firstChild = INVALID_NODE_INDEX;
        }
    }

    // Update root index
    rootIndex = newIndices[0];

    // Clear build data
    buildNodes.clear();
    buildNodes.shrink_to_fit();

    compacted = true;
}

const Sv64Node* Sv64::findLeaf(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= resolution || y >= resolution || z >= resolution) return nullptr;

    uint64_t mortonCode = morton3d::encodeUnsigned(x, y, z);

    if (compacted) {
        // Use compact traversal with popcount-based indexing
        uint32_t nodeIndex = rootIndex;

        for (uint32_t level = 0; level < maxDepth; ++level) {
            uint32_t childIdx = morton3d::childIndex64FromCode(mortonCode, maxDepth, level);

            const Sv64Node& node = nodes[nodeIndex];

            // Check if this child exists
            if ((node.childMask & (1ull << childIdx)) == 0ull) {
                return nullptr;
            }

            if (node.firstChild == INVALID_NODE_INDEX) {
                return nullptr;
            }

            // Use popcount to find offset into compact child array
            uint32_t offset = childOffset(node.childMask, childIdx);
            nodeIndex = node.firstChild + offset;
        }

        const Sv64Node& leaf = nodes[nodeIndex];
        if (leaf.occupancy <= 0.0f) return nullptr;

        return &leaf;
    } else {
        // Use build-time traversal (returns nullptr - must compact first)
        // This maintains const correctness since we can't modify nodes during findLeaf
        return nullptr;
    }
}

void buildSv64FromDense(const float* density, const uint32_t* materials, uint32_t resolution, const glm::vec3& origin, float voxelSize, Sv64& sv64) {
    sv64 = Sv64(resolution, origin, voxelSize);

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                size_t idx = x + y * resolution + z * resolution * resolution;
                float d = density[idx];
                if (d > 0.0f) {
                    uint32_t mat = materials[idx];
                    sv64.insertVoxel(x, y, z, mat, d);
                }
            }
        }
    }

    sv64.compact();
}

}