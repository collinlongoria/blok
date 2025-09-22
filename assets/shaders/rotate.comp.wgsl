// text compute shader. rotates verts in place
struct Uniforms {
    t : f32,
    N : u32,
    _pad0 : u32,
    _pad1 : u32,
};

struct Vertex {
    pos : vec4<f32>,
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> u : Uniforms;
@group(0) @binding(1) var<storage, read_write> verts : array<Vertex>;

// 64 threads per workgroup
@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {
    let i = gid.x;
    if(i >= u.N) { return; }

    let angle = u.t + f32(i) * 0.2;
    let c = cos(angle);
    let s = sin(angle);

    var v = verts[i];
    let p = v.pos.xy;
    let pr = vec2<f32>(c*p.x - s*p.y, s*p.x + c*p.y);
    v.pos = vec4<f32>(pr, v.pos.z, v.pos.w);
    verts[i] = v;
}