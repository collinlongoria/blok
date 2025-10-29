#version 460
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inUV;
layout(location=0) out vec3 vUV;
void main() {
    vUV = inUV;
    gl_Position = vec4(inPos, 1.0);
}