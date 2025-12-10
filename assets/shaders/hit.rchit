/*
* File: hit.rchit
* Project: blok
* Author: Collin Longoria
* Created on: 12/1/2025
*/

#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
    vec3 color;         // Accumulated radiance (not used in hit shader)
    vec3 throughput;    // Path throughput
    vec3 normal;        // Surface normal at hit
    vec3 hitPos;        // World position of hit
    vec3 albedo;        // Surface albedo
    float roughness;    // Surface roughness (for GGX)
    float metallic;     // Metallic factor
    float hitT;         // Hit distance (-1 if miss)
    uint rngState;      // Random state
};

layout(location = 0) rayPayloadInEXT RayPayload payload;

// this is passed here from intersection shader
struct HitAttribs {
    vec3 normal;
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

// Unpack material properties from packed flags
void unpackMaterial(MaterialGpu mat,
                    out vec3 albedo,
                    out float roughness,
                    out float metallic,
                    out uint matType,
                    out float alpha,
                    out float specular,
                    out vec3 emission,
                    out float ior) {
    albedo = mat.albedo;
    emission = mat.emission;
    ior = mat.ior;

    // Unpack flags
    metallic  = float((mat.packedFlags >> 24) & 0xFFu) / 255.0;
    roughness = float((mat.packedFlags >> 16) & 0xFFu) / 255.0;
    matType   = (mat.packedFlags >> 12) & 0xFu;
    alpha     = float((mat.packedFlags >> 8) & 0xFu) / 15.0;
    specular  = float(mat.packedFlags & 0xFFu) / 255.0;

    // Clamp roughness to minimum for stability
    roughness = max(roughness, 0.04);
}

// Simple material lookup with basic PBR properties
void getMaterialProperties(uint materialId,
                           out vec3 albedo,
                           out float roughness,
                           out float metallic,
                           out vec3 emission) {
    // Bounds check with fallback to default material
    MaterialGpu mat = materials[min(materialId, 65535u)];

    uint matType;
    float alpha, specular, ior;
    unpackMaterial(mat, albedo, roughness, metallic, matType, alpha, specular, emission, ior);

    // Handle emissive materials - add emission to payload
    if (matType == MAT_TYPE_EMISSIVE) {
        // Emission is pre-multiplied in the buffer
        // emission is already set from unpack
    } else {
        emission = vec3(0.0);
    }

    // Handle glass materials
    if (matType == MAT_TYPE_GLASS) {
        // For now, treat glass as mostly transparent diffuse
        albedo *= alpha;
        roughness = max(roughness, 0.1);
    }
}

void main() {
    // get surface data from intersection shader
    vec3 normal = normalize(hitAttribs.normal);
    uint materialId = hitAttribs.materialId;

    // compute hit position
    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // look up material properties from buffer
    vec3 albedo;
    float roughness, metallic;
    vec3 emission;
    getMaterialProperties(materialId, albedo, roughness, metallic, emission);

    // populate payload data
    payload.normal = normal;
    payload.hitPos = hitPos;
    payload.albedo = albedo;
    payload.roughness = roughness;
    payload.metallic = metallic;
    payload.hitT = gl_HitTEXT;

    // handle emissives
    if (length(emission) > 0.0) {
        payload.color = emission;  // direct emission contribution
    }
}
