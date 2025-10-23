/*
* File: object.hpp
* Project: blok
* Author: ${AUTHOR}
* Created on: 10/22/2025
*
* Copyright (c) 2025 Collin Longoria
*
* This software is released under the MIT License.
* https://opensource.org/licenses/MIT
*/

#ifndef BLOK_OBJECT_HPP
#define BLOK_OBJECT_HPP
#include <string>
#include <vector>

#include "descriptor_system.hpp"

namespace blok {

struct Material {
    std::string pipelineName;
    std::vector<vk::DescriptorSet> sets;
};

struct MeshBuffers {
    vk::Buffer vertex{}; uint64_t vertexOffset = 0; uint32_t vertexCount = 0;
    vk::Buffer index{}; uint64_t indexOffset = 0; uint32_t indexCount = 0;
};

struct Object {
    MeshBuffers mesh;
    Material material;
};

}

#endif //BLOK_OBJECT_HPP