#version 450

layout(location=0) in vec2 in_pos;
layout(location=1) in vec2 in_uv;

layout(location = 0) out vec2 uv;

void main(){
    uv = in_uv;
    gl_Position = vec4(in_pos, 0.0, 1.0);
}