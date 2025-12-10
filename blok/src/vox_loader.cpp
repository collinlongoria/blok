/*
* File: vox_loader.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/

/*
 * Useful documentation: https://paulbourke.net/dataformats/vox/
 */

#include "vox_loader.hpp"

#include <fstream>
#include <cstring>
#include <iostream>

#include "chunk_manager.hpp"

namespace blok {

// Default MagicaVoxel palette (used if VOX file doesn't include one)
// TODO this is in the file i believe, not sure if this is actually needed.
static const uint32_t DEFAULT_PALETTE[256] = {
    0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff,
    0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
    0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff,
    0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
    0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc,
    0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
    0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc,
    0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
    0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc,
    0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
    0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999,
    0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
    0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099,
    0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
    0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66,
    0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
    0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366,
    0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
    0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33,
    0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
    0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633,
    0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
    0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00,
    0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
    0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600,
    0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
    0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000,
    0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
    0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700,
    0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
    0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd,
    0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
};

// file helpers
template<typename T>
static bool readValue(std::ifstream& file, T& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
    return file.good();
}

static bool readBytes(std::ifstream& file, void* buffer, size_t count) {
    file.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(count));
    return file.good();
}

static std::string readString(std::ifstream& file) {
    int32_t len;
    if (!readValue(file, len) || len <= 0 || len > 1024) return "";
    std::string str(len, '\0');
    readBytes(file, str.data(), len);
    return str;
}

static std::unordered_map<std::string, std::string> readDict(std::ifstream& file) {
    std::unordered_map<std::string, std::string> dict;
    int32_t numPairs;
    if (!readValue(file, numPairs)) return dict;

    for (int32_t i = 0; i < numPairs; ++i) {
        std::string key = readString(file);
        std::string value = readString(file);
        if (!key.empty()) {
            dict[key] = value;
        }
    }
    return dict;
}

struct VoxChunkHeader {
    char id[4];
    int32_t contentSize;
    int32_t childrenSize;
};

static MaterialType parseVoxMaterialType(const std::string& typeStr) {
    if (typeStr == "_diffuse") return MaterialType::Diffuse;
    if (typeStr == "_metal") return MaterialType::Metallic;
    if (typeStr == "_glass") return MaterialType::Glass;
    if (typeStr == "_emit") return MaterialType::Emissive;
    return MaterialType::Diffuse;
}

static float parseFloat(const std::string& str, float defaultVal = 0.0f) {
    try {
        return std::stof(str);
    } catch (...) {
        return defaultVal;
    }
}

Material VoxFile::getMaterial(uint8_t paletteIndex) const {
    Material mat;

    // Get color from palette
    uint8_t r, g, b, a;
    getPaletteRGBA(paletteIndex, r, g, b, a);
    mat.albedo = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
    mat.alpha = a / 255.0f;

    // Apply material properties if available
    const VoxMaterial& voxMat = materials[paletteIndex];
    if (voxMat.hasProperties) {
        mat.type = voxMat.type;
        mat.roughness = voxMat.roughness;
        mat.metallic = voxMat.metallic;
        mat.ior = voxMat.ior;
        mat.specular = voxMat.specular;
        mat.alpha = voxMat.alpha;

        if (voxMat.type == MaterialType::Emissive) {
            mat.emission = mat.albedo;
            mat.emissionPower = voxMat.emission > 0 ? voxMat.emission : voxMat.flux;
            if (mat.emissionPower <= 0) mat.emissionPower = 5.0f; // Default
        }
    } else {
        // Default material based on color (simple heuristic)
        mat.type = MaterialType::Diffuse;
        mat.roughness = 0.5f;
        mat.metallic = 0.0f;
    }

    mat.voxPaletteIndex = static_cast<int16_t>(paletteIndex);
    return mat;
}

bool loadVoxFile(const std::string &filepath, VoxFile &outVox, std::string &errorMsg) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        errorMsg = "Failed to open file " + filepath;
        return false;
    }

    // read magic number
    char magic[4];
    if (!readBytes(file, magic, 4) || std::memcmp(magic, "VOX ", 4) != 0) {
        errorMsg = "Invalid VOX file: bad magic number";
        return false;
    }

    // read version
    int32_t version;
    if (!readValue(file, version)) {
        errorMsg = "Failed to read VOX version";
        return false;
    }

    if (version < 150) {
        errorMsg = "Unsupported VOX version: " + std::to_string(version) + " (need >= 150)";
        return false;
    }

    // init with default palette
    std::memcpy(outVox.palette, DEFAULT_PALETTE, sizeof(DEFAULT_PALETTE));
    outVox.models.clear();

    // current model being parsed
    VoxModel currentModel{};
    bool hasSize = false;

    // read MAIN chunk header
    VoxChunkHeader mainHeader;
    if (!readBytes(file, mainHeader.id, 4) || std::memcmp(mainHeader.id, "MAIN", 4) != 0) {
        errorMsg = "Invalid VOX file: missing MAIN chunk";
        return false;
    }
    if (!readValue(file, mainHeader.contentSize) || !readValue(file, mainHeader.childrenSize)) {
        errorMsg = "Failed to read MAIN chunk header";
        return false;
    }

    // Skip MAIN content (should be 0)
    if (mainHeader.contentSize > 0) {
        file.seekg(mainHeader.contentSize, std::ios::cur);
    }
    // Read child chunks
    std::streampos endPos = file.tellg();
    endPos += mainHeader.childrenSize;

    while (file.tellg() < endPos && file.good()) {
        VoxChunkHeader chunkHeader;
        if (!readBytes(file, chunkHeader.id, 4)) break;
        if (!readValue(file, chunkHeader.contentSize)) break;
        if (!readValue(file, chunkHeader.childrenSize)) break;

        std::streampos chunkEnd = file.tellg();
        chunkEnd += chunkHeader.contentSize;

        // Handle chunk types
        if (std::memcmp(chunkHeader.id, "SIZE", 4) == 0) {
            // New model size
            if (hasSize && !currentModel.voxels.empty()) {
                // Save previous model
                outVox.models.push_back(std::move(currentModel));
                currentModel = VoxModel{};
            }

            int32_t x, y, z;
            readValue(file, x);
            readValue(file, y);
            readValue(file, z);

            currentModel.sizeX = static_cast<uint32_t>(x);
            currentModel.sizeY = static_cast<uint32_t>(y);
            currentModel.sizeZ = static_cast<uint32_t>(z);
            hasSize = true;
        }
        else if (std::memcmp(chunkHeader.id, "XYZI", 4) == 0) {
            // Voxel data
            int32_t numVoxels;
            if (!readValue(file, numVoxels)) {
                errorMsg = "Failed to read voxel count";
                return false;
            }

            currentModel.voxels.reserve(static_cast<size_t>(numVoxels));

            for (int32_t i = 0; i < numVoxels; ++i) {
                uint8_t x, y, z, colorIndex;
                readValue(file, x);
                readValue(file, y);
                readValue(file, z);
                readValue(file, colorIndex);

                VoxVoxel v;
                v.x = x;
                v.y = y;
                v.z = z;
                v.colorIndex = colorIndex;
                currentModel.voxels.push_back(v);
            }
        }
        else if (std::memcmp(chunkHeader.id, "RGBA", 4) == 0) {
            // Custom palette
            // index 0 is unused
            for (int i = 0; i < 255; ++i) {
                uint32_t rgba;
                readValue(file, rgba);
                outVox.palette[i + 1] = rgba;
            }
            uint32_t unused;
            readValue(file, unused);
        }
        else if (std::memcmp(chunkHeader.id, "MATL", 4) == 0) {
            // Material properties - NEW
            int32_t materialId;
            if (!readValue(file, materialId)) {
                file.seekg(chunkEnd);
                continue;
            }

            auto props = readDict(file);

            if (materialId >= 0 && materialId < 256) {
                VoxMaterial& mat = outVox.materials[materialId];
                mat.hasProperties = true;

                // Parse type
                auto typeIt = props.find("_type");
                if (typeIt != props.end()) {
                    mat.type = parseVoxMaterialType(typeIt->second);
                }

                // Parse properties
                auto roughIt = props.find("_rough");
                if (roughIt != props.end()) {
                    mat.roughness = parseFloat(roughIt->second, 0.5f);
                }

                auto metalIt = props.find("_metal");
                if (metalIt != props.end()) {
                    mat.metallic = parseFloat(metalIt->second, 0.0f);
                }

                auto iorIt = props.find("_ior");
                if (iorIt != props.end()) {
                    mat.ior = parseFloat(iorIt->second, 1.5f);
                }

                auto emitIt = props.find("_emit");
                if (emitIt != props.end()) {
                    mat.emission = parseFloat(emitIt->second, 0.0f);
                }

                auto fluxIt = props.find("_flux");
                if (fluxIt != props.end()) {
                    mat.flux = parseFloat(fluxIt->second, 0.0f);
                }

                auto alphaIt = props.find("_alpha");
                if (alphaIt != props.end()) {
                    mat.alpha = parseFloat(alphaIt->second, 1.0f);
                }

                auto specIt = props.find("_sp");
                if (specIt != props.end()) {
                    mat.specular = parseFloat(specIt->second, 0.5f);
                }

                auto glowIt = props.find("_g");
                if (glowIt != props.end()) {
                    mat.glow = parseFloat(glowIt->second, 0.0f);
                }
            }
        }

        // Seek to end of chunk
        file.seekg(chunkEnd);

        // Skip children
        if (chunkHeader.childrenSize > 0) {
            file.seekg(chunkHeader.childrenSize, std::ios::cur);
        }
    }

    // last model
    if (hasSize || !currentModel.voxels.empty()) {
        outVox.models.push_back(std::move(currentModel));
    }

    if (outVox.models.empty()) {
        errorMsg = "No models found in VOX file";
        return false;
    }

    std::cout << "Loaded VOX file: " << filepath << "\n";
    std::cout << "  Models: " << outVox.models.size() << "\n";
    for (size_t i = 0; i < outVox.models.size(); ++i) {
        const auto& m = outVox.models[i];
        std::cout << "  Model " << i << ": " << m.sizeX << "x" << m.sizeY << "x" << m.sizeZ
                  << " (" << m.voxels.size() << " voxels)\n";
    }

    int matCount = 0;
    for (int i = 0; i < 256; ++i) {
        if (outVox.materials[i].hasProperties) matCount++;
    }
    if (matCount > 0) {
        std::cout << "  Materials with properties: " << matCount << "\n";
    }

    return true;

}

void importVoxMaterials(
    const VoxFile& vox,
    MaterialLibrary& matLib,
    std::array<uint32_t, 256>& paletteToMaterial
) {
    // Create materials for each palette entry
    for (int i = 1; i < 256; ++i) { // Index 0 is empty
        Material mat = vox.getMaterial(static_cast<uint8_t>(i));

        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "vox_mat_%d", i);
        mat.name = nameBuf;

        uint32_t matId = matLib.addMaterial(mat);
        paletteToMaterial[i] = matId;
        matLib.setVoxPaletteMapping(static_cast<uint8_t>(i), matId);
    }
    paletteToMaterial[0] = 0; // Empty maps to default
}

uint32_t importVoxToChunks(
    const VoxFile& vox,
    ChunkManager& chunkMgr,
    const glm::vec3& worldOffset,
    uint32_t modelIndex
) {
    if (modelIndex >= vox.models.size()) {
        std::cerr << "Invalid model index: " << modelIndex << " (only " << vox.models.size() << " models)\n";
        return 0;
    }

    const VoxModel& model = vox.models[modelIndex];
    uint32_t count = 0;

    // Check if we have material library access via chunk manager
    MaterialLibrary* matLib = chunkMgr.materialLib;

    for (const auto& v : model.voxels) {
        // VOX uses Y-up, Z-forward coordinate system
        glm::vec3 worldPos = worldOffset + glm::vec3(
            static_cast<float>(v.x),
            static_cast<float>(v.z),  // VOX Z -> our Y (up)
            static_cast<float>(v.y)   // VOX Y -> our Z (forward)
        );

        if (matLib) {
            // Use material system
            uint32_t materialId = matLib->getMaterialFromVoxPalette(v.colorIndex);
            chunkMgr.setVoxelMaterial(worldPos, materialId, 1.0f);
        } else {
            // Fallback: use color directly
            uint8_t r, g, b;
            vox.getPaletteRGB(v.colorIndex, r, g, b);
            chunkMgr.setVoxel(worldPos, r, g, b, 1.0f);
        }
        count++;
    }

    std::cout << "Imported " << count << " voxels from model " << modelIndex << "\n";
    return count;
}

bool loadAndImportVox(
    const std::string& filepath,
    ChunkManager& chunkMgr,
    MaterialLibrary* materialLib,
    const glm::vec3& worldOffset,
    uint32_t modelIndex,
    std::string* errorMsg
) {
    VoxFile vox;
    std::string err;

    if (!loadVoxFile(filepath, vox, err)) {
        if (errorMsg) *errorMsg = err;
        std::cerr << "Failed to load VOX: " << err << "\n";
        return false;
    }

    // Import materials if library provided
    if (materialLib) {
        std::array<uint32_t, 256> paletteMapping;
        importVoxMaterials(vox, *materialLib, paletteMapping);

        // Ensure chunk manager knows about the material library
        chunkMgr.setMaterialLibrary(materialLib);
    }

    uint32_t count = importVoxToChunks(vox, chunkMgr, worldOffset, modelIndex);
    return count > 0;
}


}