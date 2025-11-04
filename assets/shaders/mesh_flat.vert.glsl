#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 vUV;

// Matches C++ FrameUBO { mat4 view; mat4 proj; float time; vec3 __pad; }
layout(set = 0, binding = 0, std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    float time;
    vec3 pad;
} frameUBO;

// Matches C++ ObjectUBO { mat4 model; }
layout(set = 1, binding = 0, std140) uniform ObjectUBO {
    mat4 model;
} objectUBO;

void main()
{
    vUV = inUV;
    mat4 vp = frameUBO.proj * frameUBO.view;
    gl_Position = vp * objectUBO.model * vec4(inPos, 1.0);
}
