#version 460

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uAlbedo;

void main()
{
    vec4 texel = texture(uAlbedo, vUV);

    outColor = texel;
}
