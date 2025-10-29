#version 460
layout(location=0) in vec3 vUV;
layout(location=0) out vec4 outColor;
void main() {
    outColor = vec4(vUV, 1.0);
}