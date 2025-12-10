/*
* File: ${FILE}.${EXTENSION}
* Project: ${PROJECT}
* Author: Collin
* Created on: 12/10/2025
*/

#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <array>
#include <cstdint>
#include <string>

#include "common.hpp"
#include "vec3.hpp"

namespace blok {

enum class MaterialType : uint8_t {
    Diffuse = 0,
    Metallic = 1,
    Glass = 2,
    Emissive = 3,

    Count = 4
};

// CPU side material
struct Material {
    std::string name;

    glm::vec3 albedo{1.0f};
    float alpha{1.0f};

    float metallic{0.0f};
    float roughness{0.5f};
    float ior{1.5f}; // index of refraction
    float specular{0.5f};

    glm::vec3 emission{0.0f};
    float emissionPower{0.0f};

    MaterialType type{MaterialType::Diffuse};

    // This is exclusively for VOX files
    int16_t voxPaletteIndex{-1};

    // Some default material helpers:
    static Material createDiffuse(const glm::vec3& color, float roughness = 0.5f) {
        Material m;
        m.albedo = color;
        m.roughness = roughness;
        m.metallic = 0.0f;
        m.type = MaterialType::Diffuse;
        return m;
    }

    static Material createMetal(const glm::vec3& color, float roughness = 0.3f) {
        Material m;
        m.albedo = color;
        m.roughness = roughness;
        m.metallic = 1.0f;
        m.type = MaterialType::Metallic;
        return m;
    }

    static Material createGlass(const glm::vec3& tint, float ior = 1.5f, float roughness = 0.0f) {
        Material m;
        m.albedo = tint;
        m.roughness = roughness;
        m.ior = ior;
        m.alpha = 0.1f; // Mostly transparent
        m.type = MaterialType::Glass;
        return m;
    }

    static Material createEmissive(const glm::vec3& color, float power = 10.0f) {
        Material m;
        m.albedo = color;
        m.emission = color;
        m.emissionPower = power;
        m.type = MaterialType::Emissive;
        return m;
    }
};

struct alignas(16) MaterialGpu {
    glm::vec3 albedo; //12
    uint32_t flags; //4
    // bits 0-7 = specular, bits 8-11 = alpha, bits 12-15 = type, bits 16-23 = roughness, bits 24-31 = metallic

    glm::vec3 emission;
    float ior;

    static MaterialGpu pack(const Material& mat) {
        MaterialGpu gpu;
        gpu.albedo = mat.albedo;

        uint32_t metalBits = static_cast<uint32_t>(glm::clamp(mat.metallic, 0.0f, 1.0f) * 255.0f);
        uint32_t roughBits = static_cast<uint32_t>(glm::clamp(mat.roughness, 0.0f, 1.0f) * 255.0f);
        uint32_t typeBits = static_cast<uint32_t>(mat.type);
        uint32_t alphaBits = static_cast<uint32_t>(glm::clamp(mat.alpha, 0.0f, 1.0f) * 15.0f);
        uint32_t specBits = static_cast<uint32_t>(glm::clamp(mat.specular, 0.0f, 1.0f) * 255.0f);

        gpu.flags = (metalBits << 24) | (roughBits << 16) | (typeBits << 12) | (alphaBits << 8) | specBits;

        gpu.emission = mat.emission * mat.emissionPower;
        gpu.ior = (mat.type == MaterialType::Glass) ? mat.ior : mat.emissionPower;

        return gpu;
    }
};
static_assert(sizeof(MaterialGpu) == 32, "Must remain aligned!");

class MaterialLibrary {
public:
    MaterialLibrary();
    ~MaterialLibrary() = default;

    uint32_t addMaterial(const Material& mat);

    uint32_t addOrFindMaterial(const Material& mat);

    [[nodiscard]]
    const Material* getMaterial(uint32_t id) const;
    Material* getMaterial(uint32_t id);

    [[nodiscard]]
    const Material* getMaterialByName(const std::string& name) const;
    [[nodiscard]]
    uint32_t getMaterialIdByName(const std::string& name) const;

    uint32_t getOrCreateFromColor(uint8_t r, uint8_t g, uint8_t b);
    uint32_t getOrCreateFromColor(uint32_t packedRGB);

    void setVoxPaletteMapping(uint8_t paletteIndex, uint32_t materialId);
    [[nodiscard]]
    uint32_t getMaterialFromVoxPalette(uint8_t paletteIndex) const;

    [[nodiscard]]
    const std::vector<Material>& getMaterials() const { return m_materials; }

    [[nodiscard]]
    std::vector<MaterialGpu> packForGpu() const;

    [[nodiscard]]
    size_t size() const { return m_materials.size(); }

    void clear();

    static constexpr uint32_t MATERIAL_DEFAULT = 0;
    static constexpr uint32_t MATERIAL_MISSING = 0;

private:
    std::vector<Material> m_materials;
    std::unordered_map<std::string, uint32_t> m_nameToId;
    std::unordered_map<uint32_t, uint32_t> m_colorToId;

    std::array<uint32_t, 256> m_voxPaletteMap{};

    void createDefaultMaterials();
};

}

#endif //MATERIAL_HPP