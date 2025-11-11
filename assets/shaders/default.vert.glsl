#version 460

#define MAX_LIGHTS 10
struct Light {
    int isActive;
    int type;

    vec3 position;
    float radius;

    vec3 direction;
    float cutoff;

    vec3 color;
    float intensity;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNrm;
layout(location = 2) out vec2 vUV;

layout(set = 0, binding = 0, std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    float time;
    Light lights[MAX_LIGHTS];
    int lightCount;
    vec3 cameraPos;
} frame;

layout(set = 1, binding = 0, std140) uniform ObjectUBO {
    mat4 model;
} object;

void main()
{
    mat4 M = object.model;
    vec4 worldPos = M * vec4(inPos, 1.0);

    vWorldPos = worldPos.xyz;
    vWorldNrm = normalize(mat3(M) * inNrm);
    vUV = inUV;

    gl_Position  = frame.proj * frame.view * worldPos;
}
