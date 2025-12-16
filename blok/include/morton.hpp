/*
* File: morton.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef MORTON_HPP
#define MORTON_HPP
#include <cstdint>
namespace blok::morton3d {

static constexpr int32_t BIAS = 1 << 20;

inline uint64_t spreadBits(uint32_t v)
{
    uint64_t x = v & 0x1fffff; // 21 bits
    x = (x | (x << 32)) & 0x1f00000000ffffULL;
    x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
    x = (x | (x << 8))  & 0x100f00f00f00f00fULL;
    x = (x | (x << 4))  & 0x10c30c30c30c30c3ULL;
    x = (x | (x << 2))  & 0x1249249249249249ULL;
    return x;
}

inline uint64_t encode(int32_t x, int32_t y, int32_t z)
{
    uint32_t xs = static_cast<uint32_t>(x + BIAS);
    uint32_t ys = static_cast<uint32_t>(y + BIAS);
    uint32_t zs = static_cast<uint32_t>(z + BIAS);

    return (spreadBits(xs)     ) |
           (spreadBits(ys) << 1) |
           (spreadBits(zs) << 2);
}

inline uint64_t encodeUnsigned(uint32_t x, uint32_t y, uint32_t z) {
    return (spreadBits(x)     ) |
           (spreadBits(y) << 1) |
           (spreadBits(z) << 2);
}

inline uint32_t compactBits(uint64_t v)
{
    v &= 0x1249249249249249ULL;
    v = (v ^ (v >> 2))  & 0x10c30c30c30c30c3ULL;
    v = (v ^ (v >> 4))  & 0x100f00f00f00f00fULL;
    v = (v ^ (v >> 8))  & 0x1f0000ff0000ffULL;
    v = (v ^ (v >> 16)) & 0x1f00000000ffffULL;
    v = (v ^ (v >> 32)) & 0x1fffffULL;
    return static_cast<uint32_t>(v);
}

inline void decode(uint64_t code, int32_t& x, int32_t& y, int32_t& z) {
    x = static_cast<int32_t>(compactBits(code     )) - BIAS;
    y = static_cast<int32_t>(compactBits(code >> 1)) - BIAS;
    z = static_cast<int32_t>(compactBits(code >> 2)) - BIAS;
}

inline void decodeUnsigned(uint64_t code, uint32_t& x, uint32_t& y, uint32_t& z) {
    x = compactBits(code     );
    y = compactBits(code >> 1);
    z = compactBits(code >> 2);
}

inline uint32_t octantFromCode(uint64_t mortonCode, uint32_t maxDepth, uint32_t level) {
    uint32_t shift = 3u * (maxDepth - 1u - level);
    return static_cast<uint32_t>((mortonCode >> shift) & 0x7ull);
}

// 64-tree helpers:

// Remaps 6-bit Morton chunk (Z1 Y1 X1 Z0 Y0 X0) -> Linear 4x4x4 (Z1 Z0 Y1 Y0 X1 X0)
// This matches the Shader's "x | y<<2 | z<<4" traversal logic
inline uint32_t reorderMortonToLinear64(uint32_t m) {
    return (m & 1u)        |  // x0 (Bit 0) -> Bit 0
           ((m & 8u) >> 2) |  // x1 (Bit 3) -> Bit 1
           ((m & 2u) << 1) |  // y0 (Bit 1) -> Bit 2
           ((m & 16u) >> 1)|  // y1 (Bit 4) -> Bit 3
           ((m & 4u) << 2) |  // z0 (Bit 2) -> Bit 4
           (m & 32u);         // z1 (Bit 5) -> Bit 5
}

inline uint32_t childIndex64FromCode(uint64_t mortonCode, uint32_t maxDepth64, uint32_t level) {
    uint32_t shift = 6u * (maxDepth64 - 1u - level);
    uint32_t rawMorton = static_cast<uint32_t>((mortonCode >> shift) & 0x3Full);
    return reorderMortonToLinear64(rawMorton);
}

inline uint32_t childIndexToMorton64(uint32_t childIndex) {
    // This is complex to reverse, usually not needed for runtime
    return 0; // Unimplemented
}

inline uint32_t coordsToChildIndex64(uint32_t lx, uint32_t ly, uint32_t lz) {
    return (lx & 0x3) | ((ly & 0x3) << 2) | ((lz & 0x3) << 4);
}

inline void childIndex64ToCoords(uint32_t childIdx, uint32_t& lx, uint32_t& ly, uint32_t& lz) {
    lx = childIdx & 0x3;
    ly = (childIdx >> 2) & 0x3;
    lz = (childIdx >> 4) & 0x3;
}

inline uint32_t computeTraversalMask64(float dirX, float dirY, float dirZ) {
    uint32_t mask = 0;
    if (dirX < 0.0f) mask |= 0x03;
    if (dirY < 0.0f) mask |= 0x0C;
    if (dirZ < 0.0f) mask |= 0x30;
    return mask;
}

inline uint32_t applyTraversalMask64(uint32_t childOrder, uint32_t mask) {
    return childOrder ^ mask;
}

inline uint64_t parentCodeOctree(uint64_t code) {
    return code >> 3;
}

inline uint64_t parentCode64Tree(uint64_t code) {
    return code >> 6;
}

inline uint64_t mortonDistance(uint64_t a, uint64_t b) {
    return (a > b) ? (a - b) : (b - a);
}

inline uint32_t lowestCommonAncestorLevel(uint64_t a, uint64_t b, uint32_t maxDepth, bool is64Tree = false) {
    uint64_t diff = a ^ b;
    if (diff == 0) return maxDepth;

    uint32_t bitsPerLevel = is64Tree ? 6 : 3;
    uint32_t level = 0;

    for (uint32_t l = 0; l < maxDepth; l++) {
        uint32_t shift = bitsPerLevel * (maxDepth - 1 - l);
        if ((diff >> shift) != 0) {
            return l;
        }
        level = l + 1;
    }
    return level;
}

}
#endif