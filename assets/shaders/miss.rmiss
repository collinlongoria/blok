#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitColor;

void main() {
    // Sky gradient
    vec3 rayDir = normalize(gl_WorldRayDirectionEXT);
    float t = 0.5 * (rayDir.y + 1.0);

    vec3 skyBottom = vec3(0.6, 0.7, 0.9);
    vec3 skyTop = vec3(0.3, 0.5, 0.8);

    hitColor = mix(skyBottom, skyTop, t);
}