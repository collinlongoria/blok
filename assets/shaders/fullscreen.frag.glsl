#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// set = 2 (material), binding = 0 (combined sampler)
layout(set = 2, binding = 0) uniform sampler2D uScene;

void main() {
    outColor = texture(uScene, vUV);
    //outColor = vec4(0.0, 1.0, 0.0, 1.0);
}