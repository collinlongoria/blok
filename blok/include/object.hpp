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

// Standard Vertex format
struct Vertex {
    Vector3 pos;
    Vector3 nrm;
    Vector2 uv;
};

struct Material {
    // Material data
    Vector3 diffuse;
    Vector3 specular;
    Vector3 emission;
    float shininess;
    int textureId;

    // baked descriptor set (set = 2)
    // TODO: change to scene=0, material=1, object=2
    vk::DescriptorSet materialSet{};
};

struct MeshBuffers {
    vk::Buffer vertex{}; uint64_t vertexOffset = 0; uint32_t vertexCount = 0;
    vk::Buffer index{}; uint64_t indexOffset = 0; uint32_t indexCount = 0;
};

struct Object {
    std::string pipelineName;

    MeshBuffers mesh;
    Material material;

    Transform model;
};

}

#endif //BLOK_OBJECT_HPP