/*
* File: shadow.rmiss
* Project: blok
* Author: Collin Longoria
* Created on: 12/10/2025
*/

#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT bool isShadowed;

void main() {
    isShadowed = false;
}