/*
* File: ${FILE}.${EXTENSION}
* Project: ${PROJECT}
* Author: Collin
* Created on: 12/10/2025
*/

#include "material.hpp"
#include <algorithm>
#include <iostream>

namespace blok {

MaterialLibrary::MaterialLibrary() {
    m_voxPaletteMap.fill(MATERIAL_DEFAULT);
    createDefaultMaterials();
}

void MaterialLibrary::createDefaultMaterials() {
    // Material 0: Default white diffuse (also used for missing/invalid materials)
    Material defaultMat;
    defaultMat.name = "default";
    defaultMat.albedo = glm::vec3(0.8f, 0.8f, 0.8f);
    defaultMat.roughness = 0.5f;
    defaultMat.metallic = 0.0f;
    defaultMat.type = MaterialType::Diffuse;
    m_materials.push_back(defaultMat);
    m_nameToId["default"] = 0;
}

uint32_t MaterialLibrary::addMaterial(const Material& mat) {
    uint32_t id = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(mat);

    if (!mat.name.empty()) {
        m_nameToId[mat.name] = id;
    }

    return id;
}

uint32_t MaterialLibrary::addOrFindMaterial(const Material& mat) {
    if (!mat.name.empty()) {
        auto it = m_nameToId.find(mat.name);
        if (it != m_nameToId.end()) {
            return it->second;
        }
    }

    return addMaterial(mat);
}

const Material* MaterialLibrary::getMaterial(uint32_t id) const {
    if (id >= m_materials.size()) {
        return &m_materials[MATERIAL_DEFAULT];
    }
    return &m_materials[id];
}

Material* MaterialLibrary::getMaterial(uint32_t id) {
    if (id >= m_materials.size()) {
        return &m_materials[MATERIAL_DEFAULT];
    }
    return &m_materials[id];
}

const Material* MaterialLibrary::getMaterialByName(const std::string& name) const {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        return &m_materials[it->second];
    }
    return nullptr;
}

uint32_t MaterialLibrary::getMaterialIdByName(const std::string& name) const {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        return it->second;
    }
    return MATERIAL_DEFAULT;
}

uint32_t MaterialLibrary::getOrCreateFromColor(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t packed = (static_cast<uint32_t>(r) << 16) |
                      (static_cast<uint32_t>(g) << 8) |
                      (static_cast<uint32_t>(b) << 0);
    return getOrCreateFromColor(packed);
}

uint32_t MaterialLibrary::getOrCreateFromColor(uint32_t packedRGB) {
    // Check if we already have a material for this color
    auto it = m_colorToId.find(packedRGB);
    if (it != m_colorToId.end()) {
        return it->second;
    }

    // Create new material from color
    float r = static_cast<float>((packedRGB >> 16) & 0xFF) / 255.0f;
    float g = static_cast<float>((packedRGB >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>(packedRGB & 0xFF) / 255.0f;

    Material mat;
    mat.albedo = glm::vec3(r, g, b);
    mat.roughness = 0.5f;
    mat.metallic = 0.0f;
    mat.type = MaterialType::Diffuse;

    // Generate a name based on color
    char nameBuf[32];
    snprintf(nameBuf, sizeof(nameBuf), "color_%06X", packedRGB);
    mat.name = nameBuf;

    uint32_t id = addMaterial(mat);
    m_colorToId[packedRGB] = id;

    return id;
}

void MaterialLibrary::setVoxPaletteMapping(uint8_t paletteIndex, uint32_t materialId) {
    m_voxPaletteMap[paletteIndex] = materialId;
}

uint32_t MaterialLibrary::getMaterialFromVoxPalette(uint8_t paletteIndex) const {
    return m_voxPaletteMap[paletteIndex];
}

std::vector<MaterialGpu> MaterialLibrary::packForGpu() const {
    std::vector<MaterialGpu> packed;
    packed.reserve(m_materials.size());

    for (const auto& mat : m_materials) {
        packed.push_back(MaterialGpu::pack(mat));
    }

    return packed;
}

void MaterialLibrary::clear() {
    m_materials.clear();
    m_nameToId.clear();
    m_colorToId.clear();
    m_voxPaletteMap.fill(MATERIAL_DEFAULT);
    createDefaultMaterials();
}

}