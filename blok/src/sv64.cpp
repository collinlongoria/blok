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
    : rootIndex(0), maxDepth(calculateDepth(resolution_)), origin(origin_), voxelSize(voxelSize_), resolution(resolution_), compacted(false) {
    lastPathIndices.resize(maxDepth + 1, INVALID_NODE_INDEX);
    clear();
}

void Sv64::clear() {
    buildNodes.clear();

    // Initialize root build node
    BuildNode root{};
    root.childMask = 0ull;
    std::memset(root.children, 0xFF, sizeof(root.children));
    root.materialId = 0u;
    root.occupancy = 0.0f;
    root.mortonPrefix = 0u;
    buildNodes.push_back(root);

    nodes.clear();
    rootIndex = 0;
    compacted = false;

    // Reset cache
    lastMortonCode = 0xFFFFFFFFFFFFFFFF;
    std::fill(lastPathIndices.begin(), lastPathIndices.end(), INVALID_NODE_INDEX);
    lastPathIndices[0] = 0;
}

uint32_t Sv64::ensureChild(uint32_t nodeIndex, uint32_t childIdx) {
    BuildNode& node = buildNodes[nodeIndex];

    // Check if child already exists
    if (node.children[childIdx] != INVALID_NODE_INDEX) {
        return node.children[childIdx];
    }

    // Allocate new child
    uint32_t newChildIndex = static_cast<uint32_t>(buildNodes.size());

    if (buildNodes.capacity() <= newChildIndex) {
        buildNodes.reserve(std::max(uint32_t(1024), newChildIndex * 2));
    }

    // Create new empty build node
    BuildNode newNode{};
    newNode.childMask = 0ull;
    std::memset(newNode.children, 0xFF, sizeof(newNode.children));
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
        clear();
    }

    uint64_t mortonCode = morton3d::encodeUnsigned(x, y, z);

    uint32_t startLevel = 0;
    if (lastMortonCode != 0xFFFFFFFFFFFFFFFF) {
        uint64_t diff = mortonCode ^ lastMortonCode;
        if (diff != 0) {
            // find the highest differing bit to determine level
#if defined(_MSC_VER)
            unsigned long idx;
            _BitScanReverse64(&idx, diff);
            uint32_t highestBitDiff = idx;
#else
            uint32_t highestBitDiff = 63 - __builtin_clzll(diff);
#endif

            // convert bit position to level index
            for (uint32_t l = 0; l < maxDepth; ++l) {
                uint32_t shift = 6u * (maxDepth - 1u - l);
                if ((diff >> shift) != 0) {
                    startLevel = l;
                    break;
                }
            }
        }
        else {
            startLevel = maxDepth;
        }
    }

    uint32_t nodeIndex = lastPathIndices[startLevel];

    // Continue traversal from divergence point
    for (uint32_t level = startLevel; level < maxDepth; ++level) {
        uint32_t childIdx = morton3d::childIndex64FromCode(mortonCode, maxDepth, level);
        nodeIndex = ensureChild(nodeIndex, childIdx);
        lastPathIndices[level + 1] = nodeIndex; // Update cache
    }

    // Update Leaf
    BuildNode& leaf = buildNodes[nodeIndex];
    leaf.materialId = materialId;
    leaf.occupancy = density;
    leaf.mortonPrefix = static_cast<uint32_t>(mortonCode & 0xFFFFFFFFull);

    lastMortonCode = mortonCode;
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
    nodes.resize(bfsOrder.size());

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
    rootIndex = newIndices[rootIndex];

    // Clear build data
    buildNodes.clear();
    buildNodes.shrink_to_fit();

    lastPathIndices.clear();

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
            if ((node.childMask & (1ull << childIdx)) == 0ull) return nullptr;
            if (node.firstChild == INVALID_NODE_INDEX) return nullptr;

            // Use popcount to find offset into compact child array
            uint32_t offset = childOffset(node.childMask, childIdx);
            nodeIndex = node.firstChild + offset;
        }

        const Sv64Node& leaf = nodes[nodeIndex];
        if (leaf.occupancy <= 0.0f) return nullptr;

        return &leaf;
    }
    return nullptr;
}

void buildSv64FromDense(const float* density, const uint32_t* materials, uint32_t resolution, const glm::vec3& origin, float voxelSize, Sv64& sv64) {
    sv64 = Sv64(resolution, origin, voxelSize);

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                size_t idx = x + y * resolution + z * resolution * resolution;
                float d = density[idx];
                if (d > 0.0f) {
                    sv64.insertVoxel(x, y, z, materials[idx], d);
                }
            }
        }
    }
    sv64.compact();
}

}