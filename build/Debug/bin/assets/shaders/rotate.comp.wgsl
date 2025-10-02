// text compute shader. rotates verts in place
struct Uniforms { t: f32, N: u32, _pad0: u32, _pad1: u32 };

struct Vertex {
  pos: vec4<f32>, // pos.xy = position, pos.zw = (cx, cy)
  col: vec4<f32>,
};

@group(0) @binding(0) var<uniform> u : Uniforms;
@group(0) @binding(1) var<storage, read_write> verts : array<Vertex>;

@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let i = gid.x;
  if (i >= u.N) { return; }

  // Center to spin around (pre-baked per-vertex)
  let center = verts[i].pos.zw;

  // Angular velocity (radians per second). Tweak as you like.
  let w = 1.0;

  // Rotate by *delta angle* this frame only
  let dtheta = u.t * w;
  let c = cos(dtheta);
  let s = sin(dtheta);

  let p = verts[i].pos.xy - center;
  let r = vec2<f32>(p.x * c - p.y * s, p.x * s + c * p.y) + center;

  verts[i].pos.x = r.x;
  verts[i].pos.y = r.y;
}
