#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload {
    vec3 color;
    vec3 throughput;
    vec3 normal;
    vec3 hitPos;
    vec3 albedo;
    float roughness;
    float metallic;
    float hitT;
    uint rngState;
};

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
    payload.hitT = -1.0;
}
