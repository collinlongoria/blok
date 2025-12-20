/*
* File: app.cpp
* Project: blok
* Author: Collin Longoria / Wes Morosan
* Created on: 9/12/2025
*/
#include "app.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>

#include "window.hpp"

#include "renderer.hpp"
#include "renderer_gl.hpp"
#include "cuda_tracer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include "imgui.h"

#include "ui.hpp"
#include "camera.hpp"
#include "chunk_manager.hpp"
#include "imgui_impl_glfw.h"
#include "scene.hpp"
#include "vox_loader.hpp"

#include "unit_tests.hpp"
#include "noise.hpp"

#define VKR reinterpret_cast<VulkanRenderer*>(m_renderer.get())

using namespace blok;
static Camera g_camera;
static Scene  g_scene;
static UI* g_ui;
static ChunkManager g_mgr(64, 1.0f);
static float lastX = 400.0f;
static float lastY = 300.0f;
static bool firstMouse = true;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
    }

    float dx = (float)xpos - lastX;
    float dy = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    g_camera.processMouse(dx, dy);
}

struct TerrainSettings {
    // Size
    int halfSize = 10000;           // Half-extent of terrain

    // Height settings
    float baseHeight = 0.0f;      // Base ground level
    float hillHeight = 30.0f;     // Max height of rolling hills
    float mountainHeight = 80.0f; // Max height of mountains
    float valleyDepth = 10.0f;    // How deep valleys can go

    // Noise scales (lower = larger features)
    float hillScale = 0.008f;     // Scale for rolling hills
    float mountainScale = 0.003f; // Scale for mountains
    float detailScale = 0.05f;    // Scale for small details

    // Feature blend
    float mountaininess = 1.0f;   // 0-1, how mountainous the terrain is

    // Structures
    int structureCount = 100;
    int minStructureHeight = 5;
    int maxStructureHeight = 50;
    int minStructureWidth = 3;
    int maxStructureWidth = 15;

    // Seed
    uint32_t seed = 12345;
};

/**
 * Get terrain height at a world position using coherent noise.
 * This function is deterministic - same (x,z) always returns same height.
 */
float getTerrainHeight(float x, float z, const TerrainSettings& settings) {
    // Layer 1: Rolling hills (large, smooth features)
    float hills = fbm2D(x * settings.hillScale, z * settings.hillScale, settings.seed, 4, 2.0f, 0.5f);
    hills = (hills + 1.0f) * 0.5f; // Normalize to [0, 1]

    // Layer 2: Mountains (ridged noise for sharp peaks)
    float mountains = ridgedNoise2D(x * settings.mountainScale, z * settings.mountainScale, settings.seed + 5000, 5);

    // Layer 3: Small details/roughness
    float detail = fbm2D(x * settings.detailScale, z * settings.detailScale, settings.seed + 10000, 2, 2.0f, 0.5f);
    detail = detail * 0.5f; // Reduce detail amplitude

    // Blend mountains based on mountaininess factor
    // Mountains appear more in certain areas (use another noise layer for distribution)
    float mountainMask = fbm2D(x * 0.002f, z * 0.002f, settings.seed + 20000, 2, 2.0f, 0.5f);
    mountainMask = (mountainMask + 1.0f) * 0.5f; // [0, 1]
    mountainMask = std::pow(mountainMask, 2.0f); // Make mountains less common
    mountainMask *= settings.mountaininess;

    // Combine layers
    float height = settings.baseHeight;
    height += hills * settings.hillHeight;
    height += mountains * mountainMask * settings.mountainHeight;
    height += detail * 3.0f; // Small surface variation

    // Add valleys in low areas
    float valleyNoise = fbm2D(x * 0.01f, z * 0.01f, settings.seed + 30000, 3, 2.0f, 0.5f);
    if (valleyNoise < -0.3f) {
        float valleyStrength = (-0.3f - valleyNoise) / 0.7f; // 0-1 in valley areas
        height -= valleyStrength * settings.valleyDepth;
    }

    return height;
}

/**
 * Get terrain color based on height and slope.
 */
void getTerrainColor(float height, float slope, uint32_t seed, std::mt19937& rng,
                     uint8_t& r, uint8_t& g, uint8_t& b) {
    // Add some random variation
    int variation = rng() % 20 - 10;

    if (height < 2.0f) {
        // Low areas: darker grass / dirt
        r = static_cast<uint8_t>(std::clamp(60 + variation, 0, 255));
        g = static_cast<uint8_t>(std::clamp(100 + variation, 0, 255));
        b = static_cast<uint8_t>(std::clamp(40 + variation, 0, 255));
    }
    else if (height < 20.0f) {
        // Normal grass
        r = static_cast<uint8_t>(std::clamp(50 + variation, 0, 255));
        g = static_cast<uint8_t>(std::clamp(140 + variation * 2, 0, 255));
        b = static_cast<uint8_t>(std::clamp(35 + variation, 0, 255));
    }
    else if (height < 50.0f) {
        // Higher elevation: rocky/brown
        float t = (height - 20.0f) / 30.0f;
        r = static_cast<uint8_t>(std::clamp(static_cast<int>(50 + t * 80 + variation), 0, 255));
        g = static_cast<uint8_t>(std::clamp(static_cast<int>(140 - t * 60 + variation), 0, 255));
        b = static_cast<uint8_t>(std::clamp(static_cast<int>(35 + t * 30 + variation), 0, 255));
    }
    else if (height < 70.0f) {
        // Mountain rock
        r = static_cast<uint8_t>(std::clamp(100 + variation, 0, 255));
        g = static_cast<uint8_t>(std::clamp(90 + variation, 0, 255));
        b = static_cast<uint8_t>(std::clamp(80 + variation, 0, 255));
    }
    else {
        // Snow caps
        r = static_cast<uint8_t>(std::clamp(230 + variation, 200, 255));
        g = static_cast<uint8_t>(std::clamp(235 + variation, 200, 255));
        b = static_cast<uint8_t>(std::clamp(240 + variation, 200, 255));
    }

    // Steep slopes are more rocky
    if (slope > 0.7f) {
        r = static_cast<uint8_t>(std::clamp(static_cast<int>(r * 0.7f + 80), 0, 255));
        g = static_cast<uint8_t>(std::clamp(static_cast<int>(g * 0.6f + 60), 0, 255));
        b = static_cast<uint8_t>(std::clamp(static_cast<int>(b * 0.6f + 50), 0, 255));
    }
}

/**
 * Generates terrain with coherent noise - hills, mountains, and structures.
 * The terrain is seamless across chunk boundaries.
 */
void generateTerrain(
    ChunkManager& mgr,
    MaterialLibrary* matLib,
    const TerrainSettings& settings = TerrainSettings{}
) {
    std::cout << "Generating terrain..." << std::endl;
    std::cout << "  Size: " << (settings.halfSize * 2) << "x" << (settings.halfSize * 2) << std::endl;
    std::cout << "  Seed: " << settings.seed << std::endl;

    std::mt19937 rng(settings.seed);
    auto startTime = std::chrono::steady_clock::now();

    int totalVoxels = 0;
    int lastProgress = -1;

    // Generate terrain
    for (int x = -settings.halfSize; x <= settings.halfSize; ++x) {
        // Progress indicator
        int progress = ((x + settings.halfSize) * 100) / (settings.halfSize * 2);
        if (progress != lastProgress && progress % 10 == 0) {
            std::cout << "  Terrain: " << progress << "%" << std::endl;
            lastProgress = progress;
        }

        for (int z = -settings.halfSize; z <= settings.halfSize; ++z) {
            // Get height at this position
            float height = getTerrainHeight(static_cast<float>(x), static_cast<float>(z), settings);
            int maxY = static_cast<int>(std::floor(height));

            // Calculate slope for coloring (approximate gradient)
            float hL = getTerrainHeight(x - 1.0f, static_cast<float>(z), settings);
            float hR = getTerrainHeight(x + 1.0f, static_cast<float>(z), settings);
            float hD = getTerrainHeight(static_cast<float>(x), z - 1.0f, settings);
            float hU = getTerrainHeight(static_cast<float>(x), z + 1.0f, settings);
            float slope = std::sqrt((hR - hL) * (hR - hL) + (hU - hD) * (hU - hD)) / 4.0f;

            // Fill column from bottom to top
            // For efficiency, we only place the surface voxel and a few below
            // (You could fill the entire column if you want caves later)
            int minY = std::max(static_cast<int>(settings.baseHeight) - 5, maxY - 3);

            for (int y = minY; y <= maxY; ++y) {
                uint8_t r, g, b;

                if (y == maxY) {
                    // Surface voxel - use height-based coloring
                    getTerrainColor(height, slope, settings.seed, rng, r, g, b);
                } else {
                    // Underground - dirt/stone
                    int depth = maxY - y;
                    if (depth < 3) {
                        // Dirt
                        r = static_cast<uint8_t>(100 + (rng() % 20));
                        g = static_cast<uint8_t>(70 + (rng() % 20));
                        b = static_cast<uint8_t>(50 + (rng() % 20));
                    } else {
                        // Stone
                        r = static_cast<uint8_t>(80 + (rng() % 30));
                        g = static_cast<uint8_t>(80 + (rng() % 30));
                        b = static_cast<uint8_t>(80 + (rng() % 30));
                    }
                }

                mgr.setVoxel(glm::vec3(x, y, z), r, g, b, 1.0f);
                totalVoxels++;
            }
        }
    }

    std::cout << "  Terrain voxels: " << totalVoxels << std::endl;

    // Generate structures on the terrain
    std::cout << "  Creating structures..." << std::endl;

    std::uniform_int_distribution<int> posDist(-settings.halfSize + 50, settings.halfSize - 50);
    std::uniform_int_distribution<int> heightDist(settings.minStructureHeight, settings.maxStructureHeight);
    std::uniform_int_distribution<int> widthDist(settings.minStructureWidth, settings.maxStructureWidth);
    std::uniform_int_distribution<int> colorDist(100, 255);

    int structuresPlaced = 0;

    for (int s = 0; s < settings.structureCount; ++s) {
        int sx = posDist(rng);
        int sz = posDist(rng);

        // Get terrain height at structure position
        float terrainHeight = getTerrainHeight(static_cast<float>(sx), static_cast<float>(sz), settings);
        int baseY = static_cast<int>(std::floor(terrainHeight)) + 1;

        // Don't place structures on very steep terrain or snow
        float hL = getTerrainHeight(sx - 5.0f, static_cast<float>(sz), settings);
        float hR = getTerrainHeight(sx + 5.0f, static_cast<float>(sz), settings);
        float hD = getTerrainHeight(static_cast<float>(sx), sz - 5.0f, settings);
        float hU = getTerrainHeight(static_cast<float>(sx), sz + 5.0f, settings);
        float slope = std::sqrt((hR - hL) * (hR - hL) + (hU - hD) * (hU - hD)) / 10.0f;

        if (slope > 0.5f || terrainHeight > 60.0f) {
            continue; // Skip this structure
        }

        int height = heightDist(rng);
        int width = widthDist(rng);

        // Random structure color
        uint8_t r = static_cast<uint8_t>(colorDist(rng));
        uint8_t g = static_cast<uint8_t>(colorDist(rng));
        uint8_t b = static_cast<uint8_t>(colorDist(rng));

        // Create the structure
        for (int y = baseY; y < baseY + height; ++y) {
            for (int dx = 0; dx < width; ++dx) {
                for (int dz = 0; dz < width; ++dz) {
                    mgr.setVoxel(glm::vec3(sx + dx, y, sz + dz), r, g, b, 1.0f);
                    totalVoxels++;
                }
            }
        }
        structuresPlaced++;
    }

    auto endTime = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "Terrain generated in " << elapsed << " seconds." << std::endl;
    std::cout << "  Total voxels: " << totalVoxels << std::endl;
    std::cout << "  Structures placed: " << structuresPlaced << std::endl;
    std::cout << "  Total chunks: " << mgr.chunks.size() << std::endl;
}

// =============================================================================
// Random Test Scene Generator
// =============================================================================
void generateRandomTestScene(
    ChunkManager& mgr,
    MaterialLibrary* matLib,
    int halfExtent = 2000,
    int voxelCount = 100000,
    uint32_t seed = 0
) {
    std::cout << "Generating random test scene..." << std::endl;
    std::cout << "  Extent: " << halfExtent << " blocks in each direction" << std::endl;
    std::cout << "  Voxel count: " << voxelCount << std::endl;

    // Setup random number generator
    std::mt19937 rng;
    if (seed == 0) {
        std::random_device rd;
        seed = rd();
    }
    rng.seed(seed);
    std::cout << "  Seed: " << seed << std::endl;

    // Distribution for positions (-halfExtent to +halfExtent)
    std::uniform_int_distribution<int> posDist(-halfExtent, halfExtent);

    // Distribution for colors (0-255)
    std::uniform_int_distribution<int> colorDist(0, 255);

    auto startTime = std::chrono::steady_clock::now();

    for (int i = 0; i < voxelCount; ++i) {
        // Random position
        float x = static_cast<float>(posDist(rng));
        float y = static_cast<float>(posDist(rng));
        float z = static_cast<float>(posDist(rng));

        // Random color
        uint8_t r = static_cast<uint8_t>(colorDist(rng));
        uint8_t g = static_cast<uint8_t>(colorDist(rng));
        uint8_t b = static_cast<uint8_t>(colorDist(rng));

        // Place the voxel
        mgr.setVoxel(glm::vec3(x, y, z), r, g, b, 1.0f);

        // Progress indicator
        if ((i + 1) % 10000 == 0) {
            std::cout << "  Placed " << (i + 1) << " / " << voxelCount << " voxels..." << std::endl;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "Random test scene generated in " << elapsed << " seconds." << std::endl;
    std::cout << "  Total chunks created: " << mgr.chunks.size() << std::endl;
}

void generateClusteredTestScene(
    ChunkManager& mgr,
    MaterialLibrary* matLib,
    int halfExtent = 2000,
    int clusterCount = 500,
    int clusterRadius = 20,
    int voxelsPerCluster = 200,
    uint32_t seed = 0
) {
    std::cout << "Generating clustered test scene..." << std::endl;

    std::mt19937 rng;
    if (seed == 0) {
        std::random_device rd;
        seed = rd();
    }
    rng.seed(seed);
    std::cout << "  Seed: " << seed << std::endl;

    std::uniform_int_distribution<int> posDist(-halfExtent, halfExtent);
    std::uniform_int_distribution<int> colorDist(50, 255); // Avoid too dark colors
    std::uniform_real_distribution<float> offsetDist(-1.0f, 1.0f);

    auto startTime = std::chrono::steady_clock::now();
    int totalVoxels = 0;

    for (int c = 0; c < clusterCount; ++c) {
        // Random cluster center
        float cx = static_cast<float>(posDist(rng));
        float cy = static_cast<float>(posDist(rng));
        float cz = static_cast<float>(posDist(rng));

        // Random base color for this cluster (with some variation per voxel)
        uint8_t baseR = static_cast<uint8_t>(colorDist(rng));
        uint8_t baseG = static_cast<uint8_t>(colorDist(rng));
        uint8_t baseB = static_cast<uint8_t>(colorDist(rng));

        // Place voxels in a roughly spherical cluster
        for (int v = 0; v < voxelsPerCluster; ++v) {
            // Random offset within cluster (roughly spherical distribution)
            float ox = offsetDist(rng) * clusterRadius;
            float oy = offsetDist(rng) * clusterRadius;
            float oz = offsetDist(rng) * clusterRadius;

            // Skip if outside sphere
            float dist = std::sqrt(ox*ox + oy*oy + oz*oz);
            if (dist > clusterRadius) continue;

            float x = cx + ox;
            float y = cy + oy;
            float z = cz + oz;

            // Slight color variation within cluster
            int variation = 30;
            uint8_t r = static_cast<uint8_t>(std::clamp(baseR + (int)(offsetDist(rng) * variation), 0, 255));
            uint8_t g = static_cast<uint8_t>(std::clamp(baseG + (int)(offsetDist(rng) * variation), 0, 255));
            uint8_t b = static_cast<uint8_t>(std::clamp(baseB + (int)(offsetDist(rng) * variation), 0, 255));

            mgr.setVoxel(glm::vec3(x, y, z), r, g, b, 1.0f);
            totalVoxels++;
        }

        if ((c + 1) % 50 == 0) {
            std::cout << "  Created " << (c + 1) << " / " << clusterCount << " clusters..." << std::endl;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "Clustered test scene generated in " << elapsed << " seconds." << std::endl;
    std::cout << "  Total voxels: " << totalVoxels << std::endl;
    std::cout << "  Total chunks: " << mgr.chunks.size() << std::endl;
}

void generateGroundWithStructures(
    ChunkManager& mgr,
    MaterialLibrary* matLib,
    int groundHalfSize = 500,
    int structureCount = 100,
    uint32_t seed = 0
) {
    TerrainSettings settings;
    settings.halfSize = groundHalfSize;
    settings.structureCount = structureCount;

    if (seed == 0) {
        std::random_device rd;
        settings.seed = rd();
    } else {
        settings.seed = seed;
    }

    generateTerrain(mgr, matLib, settings);
}

namespace blok {

App::App(GraphicsApi backend)
    : m_backend(backend) {}

App::~App() {}

void App::run() {
    runAllTests();

    init();

    update();

    shutdown();

}

void App::init() {

    switch (m_backend) {
        case GraphicsApi::OpenGL: {
            m_window = std::make_shared<Window>(800, 600, "blok", m_backend);
            GLFWwindow* gw = m_window->getGLFWwindow();

            g_ui = new UI(m_window);

            glfwMakeContextCurrent(gw);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                throw std::runtime_error("Failed to load GL with GLAD");
            }
            m_rendererGL = std::make_unique<RendererGL>(m_window);
            m_rendererGL->init();
            reinterpret_cast<RendererGL*>(m_rendererGL.get())->setUI(g_ui);

            m_cudaTracer = std::make_unique<CudaTracer>(m_window->getWidth(), m_window->getHeight());
            m_cudaTracer->init();
            break;
        }
        case GraphicsApi::Vulkan: {
            m_renderer = std::make_unique<Renderer>(1280, 720);
            auto gw = m_renderer->getWindow();
            glfwSetCursorPosCallback(gw, mouse_callback);
            glfwSetInputMode(gw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            blok::MaterialLibrary& matLib = m_renderer->getMaterialLibrary();
            g_mgr.setMaterialLibrary(&matLib);

            // Option 1: Load VOX file (original behavior)
/*
            VoxFile vox;
            std::string err;
            bool success = blok::loadAndImportVox(
                "assets/models/menger.vox",
                g_mgr,
                &matLib,
                glm::vec3(0, 0, 0),
                0,
                &err
            );
            if (!success) {
                std::cerr << "Failed to load VOX: " << err << "\n";
            }
*/
            // Option 2: Random scattered voxels (sparse, tests large world)
            // Parameters: halfExtent=2000, voxelCount=100000, seed=0 (random)
            //generateRandomTestScene(g_mgr, &matLib, 2000, 5000000, 12345);

            // Option 3: Clustered voxels (more visually interesting)
            // Parameters: halfExtent, clusterCount, clusterRadius, voxelsPerCluster, seed
             //generateClusteredTestScene(g_mgr, &matLib, 2000, 500, 20, 200, 12345);

            // Option 4: Ground plane with structures (good for shadow testing)
            // Parameters: groundHalfSize, structureCount, seed
             generateGroundWithStructures(g_mgr, &matLib, 1000, 100, 12345);

            // Prepare GPU world SVO
            m_gpuWorld = std::make_unique<WorldSv64Gpu>();
            computeWorld = std::make_unique<WorldComputeGpu>();

            std::cout << "Rebuilding dirty chunks..." << std::endl;
            auto rebuildStart = std::chrono::steady_clock::now();
            rebuildDirtyChunks(g_mgr, 9999); // Rebuild all at once for test
            auto rebuildEnd = std::chrono::steady_clock::now();
            std::cout << "Rebuild took "
                      << std::chrono::duration<double>(rebuildEnd - rebuildStart).count()
                      << " seconds." << std::endl;

            std::cout << "Packing chunks for compute..." << std::endl;
            packChunksForCompute(g_mgr, *computeWorld);

            // Upload world to Renderer
            std::cout << "Uploading to GPU..." << std::endl;
            m_renderer->addWorldCompute(*computeWorld);
            std::cout << "Scene ready!" << std::endl;
        }
            break;
    }
}

void App::update() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();

    switch (m_backend) {
    case GraphicsApi::OpenGL: {
        while (m_window && !m_window->shouldClose()) {
            auto now = clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            Window::pollEvents();

            GLFWwindow* win = m_window->getGLFWwindow();
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) g_camera.processKeyboard('W', dt);
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) g_camera.processKeyboard('S', dt);
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) g_camera.processKeyboard('A', dt);
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) g_camera.processKeyboard('D', dt);

            if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, true);

            // ImGui + present path (one swap inside endFrame)
            m_cudaTracer->drawFrame(g_camera, g_scene);
            m_rendererGL->beginFrame();
            reinterpret_cast<RendererGL*>(m_rendererGL.get())->setTexture(m_cudaTracer->getGLTex(), m_window->getWidth(), m_window->getHeight());
            m_rendererGL->drawFrame(g_camera, g_scene);

            addWindow();
            g_ui->displayData(dt);

            m_rendererGL->endFrame();
        }
        break;
    }

    case GraphicsApi::Vulkan: {
        while (!glfwWindowShouldClose(m_renderer->getWindow())) {
            auto now = clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            glfwPollEvents();

            GLFWwindow* win = m_renderer->getWindow();
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) g_camera.processKeyboard('W', dt);
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) g_camera.processKeyboard('S', dt);
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) g_camera.processKeyboard('A', dt);
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) g_camera.processKeyboard('D', dt);
            if (glfwGetKey(win, GLFW_KEY_X) == GLFW_PRESS) g_camera.processKeyboard('X', dt);
            if (glfwGetKey(win, GLFW_KEY_Z) == GLFW_PRESS) g_camera.processKeyboard('Z', dt);

            if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, true);

            float fps = 1.0f / dt;
            float ms = dt * 1000.0f;
            m_renderer->updatePerformanceData((int)fps, ms);

            m_renderer->render(g_camera, dt);
        }
        break;
    }
    }
}

void App::shutdown() {
    if (m_renderer) {
        m_renderer.reset();
        m_gpuWorld.reset();
    }
    if (m_cudaTracer) { m_cudaTracer->~CudaTracer(); m_cudaTracer.reset(); }
    if (g_ui != nullptr) { delete g_ui; }
    if (m_backend == GraphicsApi::Vulkan) {
        m_window.reset();
    }
}

} // namespace blok