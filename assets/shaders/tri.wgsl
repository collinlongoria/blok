// test shader for colored triangles
struct VSIn {
    @location(0) pos : vec4<f32>,
    @location(1) color : vec4<f32>
};

struct VSOut {
    @builtin(position) pos : vec4<f32>,
    @location(0) color : vec4<f32>
};

@vertex
fn vs_main(in: VSIn) -> VSOut {
    var o: VSOut;
    o.pos = vec4<f32>(in.pos.xy, 0.0, 1.0);
    o.color = in.color;
    return o;
}

@fragment
fn fs_main(in: VSOut) -> @location(0) vec4<f32> {
    return in.color;
}