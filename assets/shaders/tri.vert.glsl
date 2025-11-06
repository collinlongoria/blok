#version 460

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inUV;
layout(location=0) out vec3 vUV;

layout(set=0, binding=0, std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
};

layout(set=1, binding=0, std140) uniform ObjectUBO {
    mat4 model;
};

void main() {
    vUV = inUV;
    gl_Position = proj * view * model * vec4(inPos, 1.0);
}