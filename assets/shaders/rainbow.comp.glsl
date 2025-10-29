#version 460

layout(set=0, binding=0, std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    float time;
    vec3 _pad;
};

struct Vtx { float x,y,z,r,g,b; };
layout(set=1, binding=0, std430) buffer Vertices {
    Vtx v[];
};

layout(push_constant) uniform Push {
    uint vertexCount;
} pc;

vec3 rainbow(float t, vec3 p) {
    float w = t + dot(p, vec3(0.7, 0.9, 1.3));
    return 0.5 + 0.5 * vec3(sin(w), sin(w + 2.0944), sin(w + 4.18879));
}

layout(local_size_x = 64) in;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.vertexCount) return;
    vec3 rgb = rainbow(time, vec3(v[i].x, v[i].y, v[i].z));
    v[i].r = rgb.r;
    v[i].g = rgb.g;
    v[i].b = rgb.b;
}