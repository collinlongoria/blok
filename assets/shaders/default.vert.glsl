#version 460

/*
struct Light {
    int type;

    vec3 position;
    float radius;

    vec3 direction;
    float cutoff;

    vec3 color;
    float intensity;
};
*/

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 vUV;

layout(set = 0, binding = 0, std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    float time;
    vec3 pad;
} frame;

layout(set = 1, binding = 0, std140) uniform ObjectUBO {
    mat4 model;
} object;

void main()
{
    vUV = inUV;
    mat4 vp = frame.proj * frame.view;
    gl_Position = vp * object.model * vec4(inPos, 1.0);
}
