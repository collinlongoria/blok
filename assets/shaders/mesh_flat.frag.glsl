#version 460

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// Material texture at set = 2, binding = 0
layout(set = 2, binding = 0) uniform sampler2D uAlbedo;

void main()
{
    vec4 texel = texture(uAlbedo, vUV);

    //if (texel.a < 0.1)
       // discard;

    outColor = texel;
}
