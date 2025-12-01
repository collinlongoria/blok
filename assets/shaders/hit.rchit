#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitColor;

// Hit attributes - receive both normal and color from intersection shader
struct HitAttribs {
    vec3 normal;
    vec3 color;
};
hitAttributeEXT HitAttribs hitAttribs;

void main() {
    vec3 color = hitAttribs.color;
    vec3 normal = hitAttribs.normal;

    // Simple lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float NdotL = max(dot(normal, lightDir), 0.0);

    // Ambient + diffuse
    vec3 ambient = color * 0.3;
    vec3 diffuse = color * NdotL * 0.7;

    hitColor = ambient + diffuse;
}