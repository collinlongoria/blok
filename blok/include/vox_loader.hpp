/*
* File: vox_loader
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*
* Description:
*/
#ifndef VOX_LOADER_HPP
#define VOX_LOADER_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "vec3.hpp"

namespace blok {

class ChunkManager;

// single voxel from vox file
struct VoxVoxel {
    uint8_t x, y, z;
    uint8_t colorIndex; // (1-255); 0 means empty
};

// an entire model from a vox file
struct VoxModel {
    uint32_t sizeX, sizeY, sizeZ;
    std::vector<VoxVoxel> voxels;
};

// an entire file of vox models
struct VoxFile {
    std::vector<VoxModel> models;

    uint32_t palette[256]; // note: format is ABGR for all colors

    void getPaletteRGB(uint8_t index, uint8_t& r, uint8_t& g, uint8_t& b) const {
        uint32_t c = palette[index];
        r = (c >> 0) & 0xFF;
        g = (c >> 8) & 0xFF;
        b = (c >> 16) & 0xFF;
    }

    void getPaletteRGBA(uint8_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const {
        uint32_t c = palette[index];
        r = (c >> 0) & 0xFF;
        g = (c >> 8) & 0xFF;
        b = (c >> 16) & 0xFF;
        a = (c >> 24) & 0xFF;
    }
};

// load a vox file from disk
bool loadVoxFile(const std::string& filepath, VoxFile& outVox, std::string& errorMsg);

// import a vox file into a ChunkManager at a given world position
// modelIndex: which model from the VOX file to import (default 0)
// worldOffset: where to place the model in world coordinates
// returns number of voxels imported
uint32_t importVoxToChunks(
    const VoxFile& vox,
    ChunkManager& chunkMgr,
    const glm::vec3& worldOffset = glm::vec3(0.0f),
    uint32_t modelIndex = 0
);

// convinience. assumes a single model.
bool loadAndImportVox(
    const std::string& filepath,
    ChunkManager& chunkMgr,
    const glm::vec3& worldOffset = glm::vec3(0.0f),
    uint32_t modelIndex = 0,
    std::string* errorMsg = nullptr
);

}

#endif //VOX_LOADER_HPP