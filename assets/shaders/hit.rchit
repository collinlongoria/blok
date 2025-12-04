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
    vec3 color;
};
hitAttributeEXT HitAttribs hitAttribs;

// TODO need actual material system
void deriveMaterialFromColor(vec3 color, out float roughness, out float metallic) {
    // THIS FUNCTION IS A PLACE HOLDER! DO NOT BOTHER EDITING
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    float saturation = length(color - vec3(luminance));

    metallic = clamp(1.0 - saturation * 2.0, 0.0, 0.3);

    roughness = clamp(0.4 + (1.0 - luminance) * 0.3, 0.1, 0.9);

    if (color.r > 0.7 && color.g > 0.5 && color.g < 0.8 && color.b < 0.3) {
        metallic = 0.9;
        roughness = 0.3;
    }
    else if (luminance > 0.8 && saturation < 0.1) {
        metallic = 0.7;
        roughness = 0.2;
    }
    else if (luminance < 0.2) {
        metallic = 0.0;
        roughness = 0.8;
    }
}

void main() {
    // get surface data from intersection shader
    vec3 albedo = hitAttribs.color;
    vec3 normal = normalize(hitAttribs.normal);

    // compute hit position
    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // derive material properties
    float roughness, metallic;
    deriveMaterialFromColor(albedo, roughness, metallic);

    // populate payload data
    payload.normal = normal;
    payload.hitPos = hitPos;
    payload.albedo = albedo;
    payload.roughness = roughness;
    payload.metallic = metallic;
    payload.hitT = gl_HitTEXT;
}
