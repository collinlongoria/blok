#version 450

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 frag;

layout(set = 2, binding = 0) uniform sampler2D tex;

void main() {
    frag = texture(tex, uv);
}