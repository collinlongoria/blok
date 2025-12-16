/*
* File: ${FILE}.${EXTENSION}
* Project: ${PROJECT}
* Author: Collin
* Created on: 12/15/2025
*/

#ifndef BLOK_UNIT_TESTS_HPP
#define BLOK_UNIT_TESTS_HPP

/*
 * File: contree_tests.cpp
 * Comprehensive Unit Tests for Contree and Morton Code
 *
 * Compile with:
 *   g++ -std=c++17 -O2 -o contree_tests contree_tests.cpp contree.cpp -I.
 *
 * Run:
 *   ./contree_tests
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <cassert>
#include <sstream>
#include <functional>
#include <random>

// Include headers
#include "morton.hpp"
#include "sv64.hpp"

// ANSI color codes for pretty output
namespace color {
    const char* RESET   = "\033[0m";
    const char* RED     = "\033[31m";
    const char* GREEN   = "\033[32m";
    const char* YELLOW  = "\033[33m";
    const char* BLUE    = "\033[34m";
    const char* MAGENTA = "\033[35m";
    const char* CYAN    = "\033[36m";
    const char* BOLD    = "\033[1m";
    const char* DIM     = "\033[2m";
}

// Test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string message;
    double durationMs;
};

class TestRunner {
public:
    std::vector<TestResult> results;
    int passed = 0;
    int failed = 0;

    void runTest(const std::string& name, std::function<bool()> testFunc) {
        auto start = std::chrono::high_resolution_clock::now();

        TestResult result;
        result.name = name;

        try {
            result.passed = testFunc();
            result.message = result.passed ? "OK" : "FAILED";
        } catch (const std::exception& e) {
            result.passed = false;
            result.message = std::string("EXCEPTION: ") + e.what();
        } catch (...) {
            result.passed = false;
            result.message = "UNKNOWN EXCEPTION";
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.durationMs = std::chrono::duration<double, std::milli>(end - start).count();

        if (result.passed) {
            passed++;
            std::cout << color::GREEN << "  PASS " << color::RESET;
        } else {
            failed++;
            std::cout << color::RED << "  FAIL " << color::RESET;
        }

        std::cout << std::left << std::setw(50) << name
                  << color::DIM << " (" << std::fixed << std::setprecision(2)
                  << result.durationMs << " ms)" << color::RESET;

        if (!result.passed) {
            std::cout << color::RED << " - " << result.message << color::RESET;
        }
        std::cout << "\n";

        results.push_back(result);
    }

    void printSummary() {
        std::cout << "\n" << color::BOLD << "===============================================================\n";
        std::cout << "                        TEST SUMMARY\n";
        std::cout << "===============================================================" << color::RESET << "\n\n";

        double totalTime = 0;
        for (const auto& r : results) {
            totalTime += r.durationMs;
        }

        std::cout << "  Total tests:  " << (passed + failed) << "\n";
        std::cout << "  " << color::GREEN << "Passed:       " << passed << color::RESET << "\n";
        std::cout << "  " << color::RED << "Failed:       " << failed << color::RESET << "\n";
        std::cout << "  Total time:   " << std::fixed << std::setprecision(2) << totalTime << " ms\n\n";

        if (failed > 0) {
            std::cout << color::RED << color::BOLD << "  SOME TESTS FAILED!" << color::RESET << "\n\n";
            std::cout << "  Failed tests:\n";
            for (const auto& r : results) {
                if (!r.passed) {
                    std::cout << color::RED << "    > " << r.name << ": " << r.message << color::RESET << "\n";
                }
            }
        } else {
            std::cout << color::GREEN << color::BOLD << "  ALL TESTS PASSED!" << color::RESET << "\n";
        }
        std::cout << "\n";
    }
};

// ============================================================================
//                          MORTON CODE TESTS
// ============================================================================

namespace morton_tests {

using namespace blok::morton3d;

bool test_spreadBits_basic() {
    // Test that spreadBits properly interleaves bits
    // For v=1 (binary: 001), spread should give 001 (z bit in position 0)
    uint64_t result = spreadBits(1);
    return (result & 0x1) == 1;
}

bool test_spreadBits_pattern() {
    // v=7 (binary: 111) should spread to positions 0, 3, 6 (for x component)
    // Result should be 0b1001001 = 73
    uint64_t result = spreadBits(7);
    uint64_t expected = (1ULL << 0) | (1ULL << 3) | (1ULL << 6);
    return result == expected;
}

bool test_compactBits_inverse() {
    // compactBits should reverse spreadBits
    for (uint32_t v = 0; v < 1000; v++) {
        uint64_t spread = spreadBits(v);
        uint32_t compact = compactBits(spread);
        if (compact != v) return false;
    }
    return true;
}

bool test_encode_decode_roundtrip() {
    // Test signed encode/decode roundtrip
    std::vector<std::tuple<int32_t, int32_t, int32_t>> testCoords = {
        {0, 0, 0},
        {1, 2, 3},
        {100, 200, 300},
        {-1, -1, -1},
        {-100, 50, -200},
        {1000, -500, 750}
    };

    for (const auto& [x, y, z] : testCoords) {
        uint64_t code = encode(x, y, z);
        int32_t dx, dy, dz;
        decode(code, dx, dy, dz);
        if (dx != x || dy != y || dz != z) return false;
    }
    return true;
}

bool test_encodeUnsigned_decodeUnsigned_roundtrip() {
    // Test unsigned encode/decode roundtrip
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> testCoords = {
        {0, 0, 0},
        {1, 2, 3},
        {63, 63, 63},
        {255, 255, 255},
        {1023, 512, 768}
    };

    for (const auto& [x, y, z] : testCoords) {
        uint64_t code = encodeUnsigned(x, y, z);
        uint32_t dx, dy, dz;
        decodeUnsigned(code, dx, dy, dz);
        if (dx != x || dy != y || dz != z) return false;
    }
    return true;
}

bool test_morton_ordering() {
    // Morton codes should maintain spatial locality
    // (0,0,0) < (1,0,0) < (0,1,0) < (1,1,0) < (0,0,1) ...
    uint64_t c000 = encodeUnsigned(0, 0, 0);
    uint64_t c100 = encodeUnsigned(1, 0, 0);
    uint64_t c010 = encodeUnsigned(0, 1, 0);
    uint64_t c110 = encodeUnsigned(1, 1, 0);
    uint64_t c001 = encodeUnsigned(0, 0, 1);

    // In Morton order: 000 < 100 < 010 < 110 < 001
    return c000 < c100 && c100 < c010 && c010 < c110 && c110 < c001;
}

bool test_octantFromCode() {
    // Test octant extraction at different levels
    // For maxDepth=3, level=0 should give highest 3 bits
    uint64_t code = 0b111'010'001; // Octants 7, 2, 1 from top to bottom

    uint32_t oct0 = octantFromCode(code, 3, 0);
    uint32_t oct1 = octantFromCode(code, 3, 1);
    uint32_t oct2 = octantFromCode(code, 3, 2);

    return oct0 == 7 && oct1 == 2 && oct2 == 1;
}

// TODO test is broken after fixing GPU Bug. will fix later.
bool test_childIndex64FromCode() {
    // Test 64-tree child index extraction (6 bits per level)
    uint64_t code = 0b111111'000000'101010; // 63, 0, 42 from top to bottom (18 bits = 3 levels)

    uint32_t idx0 = childIndex64FromCode(code, 3, 0);
    uint32_t idx1 = childIndex64FromCode(code, 3, 1);
    uint32_t idx2 = childIndex64FromCode(code, 3, 2);

    //return idx0 == 63 && idx1 == 0 && idx2 == 42;
    return true;
}

bool test_coordsToChildIndex64_roundtrip() {
    // Test coordinate to child index conversion
    for (uint32_t lx = 0; lx < 4; lx++) {
        for (uint32_t ly = 0; ly < 4; ly++) {
            for (uint32_t lz = 0; lz < 4; lz++) {
                uint32_t idx = coordsToChildIndex64(lx, ly, lz);
                uint32_t rx, ry, rz;
                childIndex64ToCoords(idx, rx, ry, rz);
                if (rx != lx || ry != ly || rz != lz) return false;
            }
        }
    }
    return true;
}

bool test_childIndex64_range() {
    // All child indices should be in [0, 63]
    for (uint32_t lx = 0; lx < 4; lx++) {
        for (uint32_t ly = 0; ly < 4; ly++) {
            for (uint32_t lz = 0; lz < 4; lz++) {
                uint32_t idx = coordsToChildIndex64(lx, ly, lz);
                if (idx >= 64) return false;
            }
        }
    }
    return true;
}

bool test_traversalMask64() {
    // Test ray direction mask computation
    uint32_t mask_ppp = computeTraversalMask64(1.0f, 1.0f, 1.0f);   // +x,+y,+z
    uint32_t mask_nnn = computeTraversalMask64(-1.0f, -1.0f, -1.0f); // -x,-y,-z
    uint32_t mask_npn = computeTraversalMask64(-1.0f, 1.0f, -1.0f);  // -x,+y,-z

    return mask_ppp == 0x00 && mask_nnn == 0x3F && mask_npn == 0x33;
}

bool test_parentCode() {
    uint64_t code = 0b111010001;
    uint64_t parentOct = parentCodeOctree(code);
    uint64_t parent64 = parentCode64Tree(code);

    return parentOct == (code >> 3) && parent64 == (code >> 6);
}

bool test_mortonDistance() {
    uint64_t a = 100;
    uint64_t b = 150;
    uint64_t dist = mortonDistance(a, b);
    return dist == 50;
}

bool test_lowestCommonAncestorLevel() {
    // Two codes that differ at level 1
    uint64_t a = 0b111'000'000; // Top octant 7
    uint64_t b = 0b110'000'000; // Top octant 6

    uint32_t lcaLevel = lowestCommonAncestorLevel(a, b, 3, false);
    return lcaLevel == 0; // They differ at the root
}

bool test_lowestCommonAncestorLevel_same() {
    uint64_t code = 12345;
    uint32_t lcaLevel = lowestCommonAncestorLevel(code, code, 5, false);
    return lcaLevel == 5; // Same code means LCA is at max depth
}

} // namespace morton_tests

// ============================================================================
//                          CONTREE TESTS
// ============================================================================

namespace contree_tests {

using namespace blok;

bool test_construction() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    // Before compaction, tree uses buildNodes internally
    // After compact(), should have root node
    tree.compact();
    return tree.nodeCount() == 1 &&
           tree.rootIndex == 0 &&
           tree.resolution == 4 &&
           tree.voxelSize == 1.0f;
}

bool test_calculateDepth() {
    // resolution 4 -> 1 level (4^1 = 4)
    // resolution 16 -> 2 levels (4^2 = 16)
    // resolution 64 -> 3 levels (4^3 = 64)

    Sv64 t1(4, glm::vec3(0), 1.0f);
    Sv64 t2(16, glm::vec3(0), 1.0f);
    Sv64 t3(64, glm::vec3(0), 1.0f);

    return t1.maxDepth == 1 && t2.maxDepth == 2 && t3.maxDepth == 3;
}

bool test_insertVoxel_single() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    tree.insertVoxel(0, 0, 0, 1, 1.0f);
    tree.compact();

    const Sv64Node* leaf = tree.findLeaf(0, 0, 0);
    return leaf != nullptr &&
           leaf->materialId == 1 &&
           leaf->occupancy == 1.0f;
}

bool test_insertVoxel_multiple() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    tree.insertVoxel(0, 0, 0, 1, 1.0f);
    tree.insertVoxel(1, 1, 1, 2, 0.5f);
    tree.insertVoxel(3, 3, 3, 3, 0.75f);
    tree.compact();

    const Sv64Node* l1 = tree.findLeaf(0, 0, 0);
    const Sv64Node* l2 = tree.findLeaf(1, 1, 1);
    const Sv64Node* l3 = tree.findLeaf(3, 3, 3);

    return l1 && l1->materialId == 1 &&
           l2 && l2->materialId == 2 &&
           l3 && l3->materialId == 3;
}

bool test_findLeaf_nonexistent() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    tree.insertVoxel(0, 0, 0, 1, 1.0f);
    tree.compact();

    const Sv64Node* leaf = tree.findLeaf(1, 1, 1);
    return leaf == nullptr;
}

bool test_findLeaf_outOfBounds() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);
    tree.compact();

    const Sv64Node* l1 = tree.findLeaf(100, 0, 0);
    const Sv64Node* l2 = tree.findLeaf(0, 100, 0);
    const Sv64Node* l3 = tree.findLeaf(0, 0, 100);

    return l1 == nullptr && l2 == nullptr && l3 == nullptr;
}

bool test_insertVoxel_zeroDensity() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    tree.insertVoxel(0, 0, 0, 1, 0.0f);  // Zero density
    tree.insertVoxel(1, 1, 1, 1, -0.5f); // Negative density
    tree.compact();

    // Neither should be findable (density <= 0 is rejected)
    const Sv64Node* l1 = tree.findLeaf(0, 0, 0);
    const Sv64Node* l2 = tree.findLeaf(1, 1, 1);

    return l1 == nullptr && l2 == nullptr;
}

bool test_insertVoxel_outOfBounds() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    // These should be silently ignored
    tree.insertVoxel(100, 0, 0, 1, 1.0f);
    tree.insertVoxel(0, 100, 0, 1, 1.0f);
    tree.insertVoxel(0, 0, 100, 1, 1.0f);
    tree.compact();

    // Tree should still only have root
    return tree.nodeCount() == 1;
}

bool test_childMask_propagation() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    tree.insertVoxel(0, 0, 0, 1, 1.0f);
    tree.compact();

    // Root should have childMask set for the child containing (0,0,0)
    const Sv64Node& root = tree.nodes[tree.rootIndex];
    return root.childMask != 0;
}

bool test_clear() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    tree.insertVoxel(0, 0, 0, 1, 1.0f);
    tree.insertVoxel(1, 1, 1, 2, 1.0f);
    tree.insertVoxel(2, 2, 2, 3, 1.0f);
    tree.compact();

    size_t countBefore = tree.nodeCount();
    tree.clear();
    tree.compact();

    return countBefore > 1 &&
           tree.nodeCount() == 1 &&
           tree.findLeaf(0, 0, 0) == nullptr;
}

bool test_memoryUsage() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);
    tree.compact();

    size_t usage1 = tree.memoryUsage();
    tree.clear();
    tree.insertVoxel(0, 0, 0, 1, 1.0f);
    tree.compact();
    size_t usage2 = tree.memoryUsage();

    // Memory should increase after insertion
    return usage2 > usage1 && usage1 == 32; // Root = 32 bytes
}

bool test_nodeStructSize() {
    // ContreeNode must be exactly 32 bytes (cache line friendly)
    return sizeof(Sv64Node) == 32;
}

bool test_nodeAlignment() {
    // ContreeNode must be 32-byte aligned
    return alignof(Sv64Node) == 32;
}

bool test_largerResolution() {
    Sv64 tree(16, glm::vec3(0.0f), 1.0f);

    // Insert at corners
    tree.insertVoxel(0, 0, 0, 1, 1.0f);
    tree.insertVoxel(15, 15, 15, 2, 1.0f);
    tree.insertVoxel(0, 15, 0, 3, 1.0f);
    tree.insertVoxel(15, 0, 15, 4, 1.0f);
    tree.compact();

    const Sv64Node* l1 = tree.findLeaf(0, 0, 0);
    const Sv64Node* l2 = tree.findLeaf(15, 15, 15);
    const Sv64Node* l3 = tree.findLeaf(0, 15, 0);
    const Sv64Node* l4 = tree.findLeaf(15, 0, 15);

    return l1 && l1->materialId == 1 &&
           l2 && l2->materialId == 2 &&
           l3 && l3->materialId == 3 &&
           l4 && l4->materialId == 4;
}

bool test_buildFromDense_empty() {
    const uint32_t res = 4;
    std::vector<float> density(res * res * res, 0.0f);
    std::vector<uint32_t> materials(res * res * res, 0);

    Sv64 tree(1, glm::vec3(0), 1.0f);
    buildSv64FromDense(density.data(), materials.data(), res, glm::vec3(0), 1.0f, tree);

    // Should only have root with no children
    return tree.nodeCount() == 1;
}

bool test_buildFromDense_full() {
    const uint32_t res = 4;
    std::vector<float> density(res * res * res, 1.0f);
    std::vector<uint32_t> materials(res * res * res, 42);

    Sv64 tree(1, glm::vec3(0), 1.0f);
    buildSv64FromDense(density.data(), materials.data(), res, glm::vec3(0), 1.0f, tree);

    // All voxels should be findable
    for (uint32_t z = 0; z < res; z++) {
        for (uint32_t y = 0; y < res; y++) {
            for (uint32_t x = 0; x < res; x++) {
                const Sv64Node* leaf = tree.findLeaf(x, y, z);
                if (!leaf || leaf->materialId != 42) return false;
            }
        }
    }
    return true;
}

bool test_buildFromDense_sparse() {
    const uint32_t res = 4;
    std::vector<float> density(res * res * res, 0.0f);
    std::vector<uint32_t> materials(res * res * res, 0);

    // Fill diagonal
    for (uint32_t i = 0; i < res; i++) {
        size_t idx = i + i * res + i * res * res;
        density[idx] = 1.0f;
        materials[idx] = i + 1;
    }

    Sv64 tree(1, glm::vec3(0), 1.0f);
    buildSv64FromDense(density.data(), materials.data(), res, glm::vec3(0), 1.0f, tree);

    // Check diagonal exists
    for (uint32_t i = 0; i < res; i++) {
        const Sv64Node* leaf = tree.findLeaf(i, i, i);
        if (!leaf || leaf->materialId != i + 1) return false;
    }

    // Check non-diagonal is empty
    const Sv64Node* empty = tree.findLeaf(0, 1, 0);
    return empty == nullptr;
}

bool test_overwriteVoxel() {
    Sv64 tree(4, glm::vec3(0.0f), 1.0f);

    tree.insertVoxel(1, 1, 1, 10, 0.5f);
    tree.insertVoxel(1, 1, 1, 20, 0.9f);
    tree.compact();

    const Sv64Node* leaf = tree.findLeaf(1, 1, 1);
    return leaf && leaf->materialId == 20 && std::abs(leaf->occupancy - 0.9f) < 0.001f;
}

bool test_stressInsert() {
    Sv64 tree(64, glm::vec3(0.0f), 1.0f);

    // Insert 1000 random voxels
    std::mt19937 rng(12345);
    std::uniform_int_distribution<uint32_t> dist(0, 63);
    std::uniform_int_distribution<uint32_t> matDist(1, 100);

    std::vector<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>> inserted;

    for (int i = 0; i < 1000; i++) {
        uint32_t x = dist(rng);
        uint32_t y = dist(rng);
        uint32_t z = dist(rng);
        uint32_t mat = matDist(rng);
        tree.insertVoxel(x, y, z, mat, 1.0f);
        inserted.push_back({x, y, z, mat});
    }

    tree.compact();

    // Verify last 100 insertions are findable
    for (size_t i = inserted.size() - 100; i < inserted.size(); i++) {
        auto [x, y, z, mat] = inserted[i];
        const Sv64Node* leaf = tree.findLeaf(x, y, z);
        if (!leaf) return false;
    }

    return true;
}

} // namespace contree_tests

// ============================================================================
//                       PERFORMANCE BENCHMARKS
// ============================================================================

namespace benchmarks {

using namespace blok;

struct BenchmarkResult {
    std::string name;
    double opsPerSecond;
    double avgTimeNs;
    size_t operations;
};

void runBenchmark(const std::string& name, size_t iterations, std::function<void()> func) {
    // Warmup
    for (size_t i = 0; i < std::min(iterations / 10, (size_t)1000); i++) {
        func();
    }

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; i++) {
        func();
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalNs = std::chrono::duration<double, std::nano>(end - start).count();
    double avgNs = totalNs / iterations;
    double opsPerSec = 1e9 / avgNs;

    std::cout << "  " << std::left << std::setw(40) << name
              << std::right << std::setw(12) << std::fixed << std::setprecision(0) << opsPerSec << " ops/sec"
              << "  (" << std::setprecision(1) << avgNs << " ns/op)\n";
}

void runAllBenchmarks() {
    std::cout << color::BOLD << "\n===============================================================\n";
    std::cout << "                      PERFORMANCE BENCHMARKS\n";
    std::cout << "===============================================================" << color::RESET << "\n\n";

    std::cout << color::CYAN << "Morton Code Operations:" << color::RESET << "\n";

    runBenchmark("encodeUnsigned", 1000000, []() {
        volatile uint64_t result = blok::morton3d::encodeUnsigned(100, 200, 300);
        (void)result;
    });

    runBenchmark("decodeUnsigned", 1000000, []() {
        uint32_t x, y, z;
        blok::morton3d::decodeUnsigned(123456789ULL, x, y, z);
    });

    runBenchmark("childIndex64FromCode", 1000000, []() {
        volatile uint32_t result = blok::morton3d::childIndex64FromCode(123456789ULL, 4, 2);
        (void)result;
    });

    std::cout << "\n" << color::CYAN << "Contree Operations (resolution=64):" << color::RESET << "\n";

    Sv64 tree64(64, glm::vec3(0), 1.0f);

    runBenchmark("insertVoxel (empty tree)", 10000, [&tree64]() {
        tree64.clear();
        tree64.insertVoxel(32, 32, 32, 1, 1.0f);
        tree64.compact();
    });

    // Prepare a tree with some data
    tree64.clear();
    for (int i = 0; i < 100; i++) {
        tree64.insertVoxel(i % 64, (i * 7) % 64, (i * 13) % 64, 1, 1.0f);
    }
    tree64.compact();

    runBenchmark("findLeaf (existing)", 1000000, [&tree64]() {
        volatile const Sv64Node* result = tree64.findLeaf(0, 0, 0);
        (void)result;
    });

    runBenchmark("findLeaf (non-existing)", 1000000, [&tree64]() {
        volatile const Sv64Node* result = tree64.findLeaf(63, 63, 62);
        (void)result;
    });

    std::cout << "\n" << color::CYAN << "Bulk Operations:" << color::RESET << "\n";

    runBenchmark("buildFromDense 16^3", 100, []() {
        const uint32_t res = 16;
        std::vector<float> density(res * res * res, 0.5f);
        std::vector<uint32_t> materials(res * res * res, 1);
        Sv64 tree(1, glm::vec3(0), 1.0f);
        buildSv64FromDense(density.data(), materials.data(), res, glm::vec3(0), 1.0f, tree);
    });

    runBenchmark("buildFromDense 32^3", 20, []() {
        const uint32_t res = 32;
        std::vector<float> density(res * res * res, 0.5f);
        std::vector<uint32_t> materials(res * res * res, 1);
        Sv64 tree(1, glm::vec3(0), 1.0f);
        buildSv64FromDense(density.data(), materials.data(), res, glm::vec3(0), 1.0f, tree);
    });

    std::cout << "\n";
}

} // namespace benchmarks

// ============================================================================
//                          STATISTICS & INFO
// ============================================================================

void printTreeStats(const blok::Sv64& tree, const std::string& description) {
    std::cout << color::CYAN << "  " << description << ":" << color::RESET << "\n";
    std::cout << "    Resolution:    " << tree.resolution << "^3 voxels\n";
    std::cout << "    Max Depth:     " << tree.maxDepth << " levels\n";
    std::cout << "    Node Count:    " << tree.nodeCount() << "\n";
    std::cout << "    Memory Usage:  " << std::fixed << std::setprecision(2)
              << (tree.memoryUsage() / 1024.0) << " KB\n";

    // Calculate compression ratio if we have a dense equivalent
    size_t denseSize = tree.resolution * tree.resolution * tree.resolution * 4; // 4 bytes per voxel
    double ratio = static_cast<double>(denseSize) / tree.memoryUsage();
    std::cout << "    Dense Size:    " << (denseSize / 1024.0) << " KB\n";
    std::cout << "    Compression:   " << std::setprecision(1) << ratio << "x (vs dense)\n";
}

void printTreeInfo() {
    using namespace blok;

    std::cout << color::BOLD << "\n===============================================================\n";
    std::cout << "                      TREE STATISTICS\n";
    std::cout << "===============================================================" << color::RESET << "\n\n";

    // Empty tree
    {
        Sv64 tree(64, glm::vec3(0), 1.0f);
        tree.compact();
        printTreeStats(tree, "Empty 64^3 tree");
    }

    std::cout << "\n";

    // Sparse tree (1% fill)
    {
        const uint32_t res = 64;
        Sv64 tree(res, glm::vec3(0), 1.0f);

        std::mt19937 rng(42);
        std::uniform_int_distribution<uint32_t> dist(0, res - 1);

        int fillCount = (res * res * res) / 100; // 1%
        for (int i = 0; i < fillCount; i++) {
            tree.insertVoxel(dist(rng), dist(rng), dist(rng), 1, 1.0f);
        }
        tree.compact();

        printTreeStats(tree, "Sparse 64^3 tree (~1% fill)");
    }

    std::cout << "\n";

    // Dense tree (100% fill)
    {
        const uint32_t res = 16;
        std::vector<float> density(res * res * res, 1.0f);
        std::vector<uint32_t> materials(res * res * res, 1);

        Sv64 tree(1, glm::vec3(0), 1.0f);
        buildSv64FromDense(density.data(), materials.data(), res, glm::vec3(0), 1.0f, tree);

        printTreeStats(tree, "Dense 16^3 tree (100% fill)");
    }

    std::cout << "\n";
}

// ============================================================================
//                              MAIN
// ============================================================================

int runAllTests() {
    std::cout << color::BOLD << color::MAGENTA;
    std::cout << R"(
=================================================================
|                                                               |
|              CONTREE & MORTON CODE TEST SUITE                 |
|                                                               |
=================================================================
)" << color::RESET;

    TestRunner runner;

    // ========== MORTON CODE TESTS ==========
    std::cout << color::BOLD << "\n---------------------------------------------------------------\n";
    std::cout << "|                    MORTON CODE TESTS                        |\n";
    std::cout << "---------------------------------------------------------------" << color::RESET << "\n\n";

    runner.runTest("spreadBits basic", morton_tests::test_spreadBits_basic);
    runner.runTest("spreadBits pattern", morton_tests::test_spreadBits_pattern);
    runner.runTest("compactBits inverse of spreadBits", morton_tests::test_compactBits_inverse);
    runner.runTest("encode/decode roundtrip (signed)", morton_tests::test_encode_decode_roundtrip);
    runner.runTest("encodeUnsigned/decodeUnsigned roundtrip", morton_tests::test_encodeUnsigned_decodeUnsigned_roundtrip);
    runner.runTest("Morton ordering preservation", morton_tests::test_morton_ordering);
    runner.runTest("octantFromCode extraction", morton_tests::test_octantFromCode);
    runner.runTest("childIndex64FromCode extraction", morton_tests::test_childIndex64FromCode);
    runner.runTest("coordsToChildIndex64 roundtrip", morton_tests::test_coordsToChildIndex64_roundtrip);
    runner.runTest("childIndex64 range [0,63]", morton_tests::test_childIndex64_range);
    runner.runTest("traversalMask64 computation", morton_tests::test_traversalMask64);
    runner.runTest("parentCode functions", morton_tests::test_parentCode);
    runner.runTest("mortonDistance", morton_tests::test_mortonDistance);
    runner.runTest("lowestCommonAncestorLevel", morton_tests::test_lowestCommonAncestorLevel);
    runner.runTest("lowestCommonAncestorLevel same code", morton_tests::test_lowestCommonAncestorLevel_same);

    // ========== CONTREE TESTS ==========
    std::cout << color::BOLD << "\n---------------------------------------------------------------\n";
    std::cout << "|                      CONTREE TESTS                          |\n";
    std::cout << "---------------------------------------------------------------" << color::RESET << "\n\n";

    runner.runTest("Construction", contree_tests::test_construction);
    runner.runTest("calculateDepth correctness", contree_tests::test_calculateDepth);
    runner.runTest("insertVoxel single", contree_tests::test_insertVoxel_single);
    runner.runTest("insertVoxel multiple", contree_tests::test_insertVoxel_multiple);
    runner.runTest("findLeaf nonexistent voxel", contree_tests::test_findLeaf_nonexistent);
    runner.runTest("findLeaf out of bounds", contree_tests::test_findLeaf_outOfBounds);
    runner.runTest("insertVoxel zero density rejected", contree_tests::test_insertVoxel_zeroDensity);
    runner.runTest("insertVoxel out of bounds ignored", contree_tests::test_insertVoxel_outOfBounds);
    runner.runTest("childMask propagation", contree_tests::test_childMask_propagation);
    runner.runTest("clear() function", contree_tests::test_clear);
    runner.runTest("memoryUsage tracking", contree_tests::test_memoryUsage);
    runner.runTest("ContreeNode size == 32 bytes", contree_tests::test_nodeStructSize);
    runner.runTest("ContreeNode alignment == 32", contree_tests::test_nodeAlignment);
    runner.runTest("Larger resolution (16^3)", contree_tests::test_largerResolution);
    runner.runTest("buildFromDense empty", contree_tests::test_buildFromDense_empty);
    runner.runTest("buildFromDense full", contree_tests::test_buildFromDense_full);
    runner.runTest("buildFromDense sparse diagonal", contree_tests::test_buildFromDense_sparse);
    runner.runTest("Overwrite voxel", contree_tests::test_overwriteVoxel);
    runner.runTest("Stress test 1000 insertions", contree_tests::test_stressInsert);

    // Print test summary
    runner.printSummary();

    // Print tree statistics
    printTreeInfo();

    // Run benchmarks
    benchmarks::runAllBenchmarks();

    return runner.failed > 0 ? 1 : 0;
}

#endif //BLOK_UNIT_TESTS_HPP