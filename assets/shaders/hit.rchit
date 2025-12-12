/*
* File: hit.rchit
* Project: blok
* Author: Collin Longoria
* Created on: 12/1/2025
*/

#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
    vec3 radiance;
    vec3 normal;
    vec3 albedo;
    float roughness;
    float metallic;
    float hitT;
};

layout(location = 0) rayPayloadInEXT RayPayload payload;

// this is passed here from intersection shader
struct HitAttribs {
    uint materialId;
};
hitAttributeEXT HitAttribs hitAttribs;

struct MaterialGpu {
    vec3 albedo;
    uint packedFlags;
    vec3 emission;
    float ior;
};

// Material type constants
const uint MAT_TYPE_DIFFUSE = 0u;
const uint MAT_TYPE_METALLIC   = 1u;
const uint MAT_TYPE_GLASS   = 2u;
const uint MAT_TYPE_EMISSIVE    = 3u;

layout(binding = 9, set = 0) readonly buffer MaterialBuffer {
    MaterialGpu materials[];
};

// Lookup table for normals based on face ID
const vec3 FACE_NORMALS[6] = vec3[](
    vec3(1, 0, 0),  // 0: +X
    vec3(-1, 0, 0), // 1: -X
    vec3(0, 1, 0),  // 2: +Y
    vec3(0, -1, 0), // 3: -Y
    vec3(0, 0, 1),  // 4: +Z
    vec3(0, 0, -1)  // 5: -Z
);

void main() {
    // 1. Get Normal from HitKind (passed from reportIntersectionEXT)
    // gl_HitKind is the 2nd argument of reportIntersectionEXT
    uint faceID = gl_HitKindEXT;
    vec3 normal = FACE_NORMALS[faceID];

    // 2. Material Lookup
    MaterialGpu mat = materials[min(hitAttribs.materialId, 65535u)];

    // Unpack
    float metallic  = float((mat.packedFlags >> 24) & 0xFFu) / 255.0;
    float roughness = float((mat.packedFlags >> 16) & 0xFFu) / 255.0;
    uint matType    = (mat.packedFlags >> 12) & 0xFu;

    // 3. Write Payload
    payload.normal = normal;
    payload.albedo = mat.albedo;
    payload.roughness = max(roughness, 0.04);
    payload.metallic = metallic;
    payload.hitT = gl_HitTEXT;
    payload.radiance = mat.emission; // Pass emission
}