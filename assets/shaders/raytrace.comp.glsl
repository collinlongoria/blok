#version 450

// workgroup size must match your C++ dispatch math (8x8 there)
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// set = 0 (compute), binding = 0: storage image
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D outImage;

// Push constants compatible with your ComputePC
layout(push_constant) uniform PC {
    int   width;
    int   height;
    float tFrame;
} pc;

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= pc.width || gid.y >= pc.height) return;

    // Simple animated gradient in [0,1]
    vec2 uv = vec2(gid) / vec2(pc.width, pc.height);
    float r = uv.x;
    float g = uv.y;
    float b = 0.5 + 0.5 * sin(pc.tFrame + 6.28318 * (uv.x - uv.y));
    vec4 color = vec4(r, g, b, 1.0);

    imageStore(outImage, gid, color);
}