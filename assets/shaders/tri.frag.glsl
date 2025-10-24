#version 460
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 outColor;
void main() {
    outColor = vec4(vUV, 0.5, 1.0);
}