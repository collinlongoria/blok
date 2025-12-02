/*
* File: brush.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include "brush.hpp"

#include "chunk_manager.hpp"

namespace blok {

void applyBrush(ChunkManager& mgr, const Brush& brush) {
    // get bounding box in world space
    glm::vec3 bbMin = brush.centerWS - glm::vec3(brush.radiusWS);
    glm::vec3 bbMax = brush.centerWS + glm::vec3(brush.radiusWS);

    // convert to voxel coords
    glm::ivec3 gvMin = mgr.worldToGlobalVoxel(bbMin);
    glm::ivec3 gvMax = mgr.worldToGlobalVoxel(bbMax);

    gvMax += glm::ivec3(1); // TODO: remove this if std::floor works fine and doesnt clip near edges

    // iterate voxel space bb
    for (int gz = gvMin.z; gz < gvMax.z; gz++)
    for (int gy = gvMin.y; gy < gvMax.y; gy++)
    for (int gx = gvMin.x; gx < gvMax.x; gx++) {
        glm::ivec3 gv(gx, gy, gz);

        // determine chunk
        ChunkCoord cc = mgr.globalVoxelToChunk(gv);
        Chunk* ch = mgr.getOrCreateChunk(cc);

        // convert to local
        glm::ivec3 lv = mgr.globalVoxelToLocal(gv, cc);
        if (lv.x < 0 || lv.x >= static_cast<int>(mgr.C) ||
            lv.y < 0 || lv.y >= static_cast<int>(mgr.C) ||
            lv.z < 0 || lv.z >= static_cast<int>(mgr.C)) continue;

        // get voxel world pos
        glm::vec3 voxelWS = ch->svo.origin + glm::vec3(static_cast<float>(lv.x) + 0.5f * mgr.voxelSize,
                                                       static_cast<float>(lv.y) + 0.5f * mgr.voxelSize,
                                                       static_cast<float>(lv.z) + 0.5f * mgr.voxelSize);

        // check if inside brush
        float dist = glm::distance(voxelWS, brush.centerWS);
        if (dist > brush.radiusWS) continue;

        // apply value
        float& d = ch->density[mgr.localIndex(lv.x, lv.y, lv.z)];
        switch (brush.mode) {
        case Brush::ADD:
            d = std::max(d, brush.value);
            break;
        case Brush::SUBTRACT:
            d = std::min(d, brush.value);
            break;
        }

        // mark chunk dirty
        ch->dirty = true;
    }
}

}
