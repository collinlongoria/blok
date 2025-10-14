#version 450

layout(local_size_x = 8, local_size_y = 8) in; // config

layout(binding = 0, rgba8) uniform writeonly image2D outImage;

layout(push_constant) uniform Push {
    int width;
    int height;
    float tFrame;
} pc;

struct Sphere {
    vec3 center;
    float radius;
    uvec4 colorRGBA8;
};

struct Plane {
    vec3 normal;
    float d;
    uvec4 colorRGBA8;
};

// SSBOs
layout(std430, binding = 1) buffer Spheres { Sphere spheres[]; };
layout(std430, binding = 2) buffer Planes { Plane planes[]; };

// Counts
layout(std140, binding = 3) uniform Counts {
    int numSpheres;
    int numPlanes;
};

// Camera UBO
layout(std140, binding = 4) uniform CameraUBO {
    vec4 camPos_fov; // xyz = pos, w = fov
    vec4 camFwd_pad; // xyz = forward
    vec4 camRight_pad; // xyz = right
    vec4 camUp_pad; // xyz = up
};

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 v) { return clamp(v, 0.0, 1.0); }

// Need to figure out how to ignore this if srgb is default (vulkan, etc)
vec3 linearToApproxSRGB(vec3 c){
    return pow(saturate(c), vec3(1.0/2.2));
}

vec3 skyGradient(vec3 rd){
    float t = 0.5 * (rd.y + 1.0);
    vec3 top = vec3(0.5, 0.7, 1.0);
    vec3 bot = vec3(1.0);
    return mix(bot, top, t);
}

bool hit_sphere(in Sphere s, vec3 ro, vec3 rd, out float tHit){
    vec3 oc = ro - s.center;
    float a = dot(rd, rd);
    float b = 2.0 * dot(oc, rd);
    float c = dot(oc, oc) - s.radius*s.radius;
    float disc = b*b - 4.0*a*c;
    if(disc < 0.0) { tHit = -1.0; return false; }
    float sq = sqrt(disc);
    float t0 = (-b - sq) / (2.0 * a);
    float t1 = (-b + sq) / (2.0 * a);
    tHit = (t0 > 1e-4) ? t0 : ((t1 > 1e-4) ? t1 : -1.0);
    return tHit > 0.0;
}

bool hit_plane(in Plane p, vec3 ro, vec3 rd, out float tHit) {
    vec3 n = normalize(p.normal);
    float denom = dot(n, rd);
    if (abs(denom) < 1e-6) { tHit = -1.0; return false; }
    float t = -(dot(n, ro) + p.d) / denom;
    if (t > 1e-4) { tHit = t; return true; }
    tHit = -1.0;
    return false;
}

struct HitInfo {
    float t;
    vec3  n;
    uvec4 baseRGBA8;
    bool  hit;
};


HitInfo traceClosest(vec3 ro, vec3 rd) {
    HitInfo h;
    h.t = 1e20;
    h.n = vec3(0,1,0);
    h.baseRGBA8 = uvec4(255,255,255,255);
    h.hit = false;

    // Spheres
    for (int i=0; i<numSpheres; ++i) {
        float tHit;
        if (hit_sphere(spheres[i], ro, rd, tHit) && tHit < h.t) {
            h.t = tHit;
            vec3 hp = ro + rd * tHit;
            h.n = normalize(hp - spheres[i].center);
            h.baseRGBA8 = spheres[i].colorRGBA8;
            h.hit = true;
        }
    }

    // Planes (checkerboard)
    for (int i=0; i<numPlanes; ++i) {
        float tHit;
        if (hit_plane(planes[i], ro, rd, tHit) && tHit < h.t) {
            h.t = tHit;
            vec3 n = normalize(planes[i].normal);
            vec3 hp = ro + rd * tHit;

            // simple checker on plane space: choose two tangent axes
            // build any orthonormal basis (n, t, b)
            vec3 tAxis = normalize(abs(n.y) < 0.999 ? cross(n, vec3(0,1,0)) : cross(n, vec3(1,0,0)));
            vec3 bAxis = cross(n, tAxis);

            float u = dot(hp, tAxis);
            float v = dot(hp, bAxis);
            float checker = mod(floor(u) + floor(v), 2.0);
            uvec4 cA = planes[i].colorRGBA8;
            uvec4 cB = uvec4(30,30,30,255);
            h.baseRGBA8 = (checker < 0.5) ? cA : cB;

            h.n = n;
            h.hit = true;
        }
    }
    return h;
}

bool traceShadow(vec3 ro, vec3 rd, float maxDist) {
    // small offset to avoid acne
    ro += rd * 1e-3;

    float t;
    // spheres
    for (int i=0; i<numSpheres; ++i) {
        if (hit_sphere(spheres[i], ro, rd, t) && t < maxDist) return true;
    }
    // planes
    for (int i=0; i<numPlanes; ++i) {
        if (hit_plane(planes[i], ro, rd, t) && t < maxDist) return true;
    }
    return false;
}

vec3 shadePixel(vec3 ro, vec3 rd, vec3 lightDir) {
    HitInfo h = traceClosest(ro, rd);
    if (!h.hit) {
        return skyGradient(rd);
    }

    vec3 hp = ro + rd * h.t;
    vec3 n  = normalize(h.n);
    vec3 l  = normalize(lightDir);
    vec3 v  = normalize(-rd);
    vec3 hlf= normalize(l + v);

    // base color from u8 -> [0,1]
    vec3 base = vec3(h.baseRGBA8.rgb) / 255.0;

    // lighting (match CUDA)
    float ambient = 0.08;
    float diff    = max(0.0, dot(n, l));
    float spec    = pow(max(0.0, dot(n, hlf)), 48.0);

    // hard shadow toggle
    bool shadowed = traceShadow(hp, l, 1e4);
    float shadowFactor = shadowed ? 0.25 : 1.0;

    vec3 lit = base * (ambient + diff * shadowFactor)
    + vec3(1.0) * (spec * 0.4 * shadowFactor);

    return lit;
}

// --------- Main ---------
void main() {
    uvec2 gid = gl_GlobalInvocationID.xy;

    if (gid.x >= uint(pc.width) || gid.y >= uint(pc.height)) return;

    // Camera
    vec3 camPos   = camPos_fov.xyz;
    float fovScale= camPos_fov.w;
    vec3 camFwd   = normalize(camFwd_pad.xyz);
    vec3 camRight = normalize(camRight_pad.xyz);
    vec3 camUp    = normalize(camUp_pad.xyz);

    // NDC pixel center in [-1,1], CUDA math reproduced
    float u = (2.0 * ((float(gid.x) + 0.5) / float(pc.width))  - 1.0) * fovScale;
    float v = (1.0 - 2.0 * ((float(gid.y) + 0.5) / float(pc.height))) * fovScale;

    vec3 rd = normalize(camFwd + camRight * u + camUp * v);
    vec3 ro = camPos;

    // light anim matches CUDA (cos(0.3 + t)*0.6 + 1.0, 1.0, -0.5)
    vec3 lightDir = normalize(vec3(cos(0.3 + pc.tFrame)*0.6 + 1.0, 1.0, -0.5));

    vec3 color = shadePixel(ro, rd, lightDir);
    color = linearToApproxSRGB(color);

    imageStore(outImage, ivec2(gid), vec4(color, 1.0));
}