#include "svo.hpp"

namespace blok {

// helper to make a blank node
static SvoNode makeEmptyNode() {
    SvoNode n{};
    n.childMask = 0u;
    n.firstChild = INVALID_NODE_INDEX;
    n.color = 0u;
    n.occupancy = 0.0f;
    return n;
}

SvoTree::SvoTree(uint32_t maxDepth_, const glm::vec3 &origin_, float voxelSize_)
    : rootIndex(0), maxDepth(maxDepth_), origin(origin_), voxelSize(voxelSize_) {
    nodes.reserve(1024);
    nodes.clear();
    nodes.push_back(makeEmptyNode());
}

void SvoTree::clear() {
    nodes.clear();
    nodes.push_back(makeEmptyNode());
    rootIndex = 0;
}

// ensures node at nodeIndex has its 8 children allocated.
// returns firstChild index
static uint32_t ensureChildren(std::vector<SvoNode>& nodes, uint32_t nodeIndex) {
    // Check if children already exist BEFORE any modification
    if (nodes[nodeIndex].firstChild != INVALID_NODE_INDEX)
        return nodes[nodeIndex].firstChild;

    const auto firstChild = static_cast<uint32_t>(nodes.size());

    // Reserve space to avoid multiple reallocations
    if (nodes.capacity() < nodes.size() + 8) {
        nodes.reserve(nodes.capacity() * 2 + 8);
    }

    nodes.resize(nodes.size() + 8);

    // initialize new children as empty
    for (uint32_t i = 0; i < 8; ++i)
        nodes[firstChild + i] = makeEmptyNode();

    // Access by index AFTER resize, not via cached reference
    nodes[nodeIndex].firstChild = firstChild;
    return firstChild;
}

void SvoTree::insertVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t color, float density) {
    if (density <= 0.0f)
        return; // TODO: ignoring empty writes for now

    // clamp to valid range
    const uint32_t dim = 1u << maxDepth;
    if (x >= dim || y >= dim || z >= dim)
        return; // OUT OF BOUNDS

    const uint64_t code = morton3d::encode(x, y, z);

    uint32_t nodeIndex = rootIndex;

    // record the path to propagate childMask upwards
    uint32_t pathNodeIndices[32]; // TODO: maxDepth <= 32 assumed
    uint32_t pathChildOctants[32];

    // descend from root to leaf parent
    for (uint32_t level = 0; level < maxDepth; ++level) {
        pathNodeIndices[level] = nodeIndex;

        const uint32_t oct = morton3d::octantFromCode(code, maxDepth, level);
        pathChildOctants[level] = oct;

        const uint32_t firstChild = ensureChildren(nodes, nodeIndex);
        const uint32_t childIndex = firstChild + oct;

        nodeIndex = childIndex;
    }

    // nodeIndex is now leaf node
    SvoNode& leaf = nodes[nodeIndex];
    leaf.color = color;
    leaf.occupancy = density;

    // propogate childMask bits up along the path
    for (int level = static_cast<int>(maxDepth) - 1; level >= 0; --level) {
        const uint32_t parentIndex = pathNodeIndices[level];
        const uint32_t oct = pathChildOctants[level];

        nodes[parentIndex].childMask |= (1u << oct);
    }
}

const SvoNode *SvoTree::findLeaf(uint32_t x, uint32_t y, uint32_t z) const {
    const uint32_t dim = 1u << maxDepth;
    if (x >= dim || y >= dim || z >= dim)
        return nullptr;

    const uint64_t code = morton3d::encode(x, y, z);

    uint32_t nodeIndex = rootIndex;

    for (uint32_t level = 0; level < maxDepth; ++level) {
        const uint32_t oct = morton3d::octantFromCode(code, maxDepth, level);
        const SvoNode& node = nodes[nodeIndex];

        if ((node.childMask & (1u << oct)) == 0u)
            return nullptr; // this subtree is empty

        if (node.firstChild == INVALID_NODE_INDEX)
            return nullptr; // logically shouldn't happen?

        nodeIndex = node.firstChild + oct;
    }

    const SvoNode& leaf = nodes[nodeIndex];
    if (leaf.occupancy <= 0.0f)
        return nullptr;

    return &leaf;
}


}