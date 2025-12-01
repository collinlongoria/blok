#ifndef MORTON_HPP
#define MORTON_HPP
#include <cstdint>
namespace blok::morton3d {

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
    static constexpr int32_t BIAS = 1 << 20; // i.e 1,048,576
    uint32_t xs = x + BIAS;
    uint32_t ys = y + BIAS;
    uint32_t zs = z + BIAS;

    return (spreadBits(xs)     ) |
           (spreadBits(ys) << 1) |
           (spreadBits(zs) << 2);
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

inline void decode(uint64_t code, int32_t& x, int32_t& y, int32_t& z)
{
    static constexpr int32_t BIAS = 1 << 20;

    x = static_cast<int32_t>(compactBits(code        )) - BIAS;
    y = static_cast<int32_t>(compactBits(code >> 1)) - BIAS;
    z = static_cast<int32_t>(compactBits(code >> 2)) - BIAS;
}

inline uint32_t octantFromCode(uint64_t mortonCode, uint32_t maxDepth, uint32_t level) {
    uint32_t shift = 3u * (maxDepth - 1u - level);
    return static_cast<uint32_t>((mortonCode >> shift) & 0x7ull);
}

}
#endif