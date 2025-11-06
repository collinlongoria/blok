#version 460

layout(location=0) in vec3 vUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0, std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
};

void main() {
    outColor = vec4(vUV, 1.0);
}