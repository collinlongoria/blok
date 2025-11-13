#version 460

#define MAX_LIGHTS 10
struct Light {
    int isActive;
    int type;

    vec3 position;
    float radius;

    vec3 direction;
    float cutoff;

    vec3 color;
    float intensity;
};

layout(set = 0, binding = 0, std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    float time;
    Light lights[MAX_LIGHTS];
    int lightCount;
    vec3 cameraPos;
} frame;

layout(set = 2, binding = 0, std140) uniform MaterialUBO {
    vec3 diffuse;
    vec3 specular;
    vec3 emission;
    float shininess;
} material;

layout(set = 2, binding = 1) uniform sampler2D uAlbedo;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNrm;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

// TODO: redo these
const float PHONG_SPEC_POWER = 32.0;
const float PI = 3.14159265;

struct LightSample {
    vec3 L;
    float NdotL;
    float attenuation;
    float spotFactor;
    vec3 color;
};

LightSample sampleLight(Light L, vec3 N, vec3 worldPos){
    LightSample s;
    s.color = L.color * L.intensity;
    s.attenuation = 1.0;
    s.spotFactor = 1.0;
    s.NdotL = 0.0;
    s.L = vec3(0.0);

    int type = L.type;

    if(type == 0 || type == 1) {
        vec3 toLight = L.position - worldPos;
        float dist = length(toLight);
        if(dist > 0.0) {
            s.L = toLight / dist;
        }
        float r = max(L.radius, 0.001);
        float x = clamp(1.0 - dist / r, 0.0, 1.0);
        s.attenuation = x * x;
    }
    else if(type == 2){
        s.L = normalize(-L.direction);
    }

    s.NdotL = max(dot(N, s.L), 0.0);

    if(type == 1) {
        vec3 dir = normalize(L.direction);
        float cosTheta = dot(-s.L, dir);
        float cosCut = L.cutoff;

        // softens edge a bit
        float eps = 0.1;
        s.spotFactor = smoothstep(cosCut, cosCut + eps, cosTheta);
    }

    return s;
}

vec3 evaluatePhong(Light L, vec3 N, vec3 V, vec3 albedo, vec3 worldPos){
    LightSample s = sampleLight(L, N, worldPos);

    if(s.NdotL <= 0.0) {
        return vec3(0.0);
    }

    vec3 H = normalize(V + s.L);
    float NdotH = max(dot(N, H), 0.0);

    float specPower = max(material.shininess, 1.0);
    float spec = pow(NdotH, specPower);

    vec3 diffuse = albedo * s.NdotL;
    vec3 specular = spec * material.specular;

    float weight = s.attenuation * s.spotFactor;
    return s.color * weight * (diffuse + specular);
}

void main()
{
    vec3 N = normalize(vWorldNrm);
    vec3 V = normalize(frame.cameraPos - vWorldPos);

    vec3 albedo = material.diffuse;
    float alpha = 1.0;

    vec4 tex = texture(uAlbedo, vUV);
    albedo *= tex.rgb;
    alpha = tex.a;

    vec3 color = vec3(0.0);

    // simple ambient
    vec3 ambient = 0.1 * albedo * material.diffuse;
    color += ambient;

    for(int i = 0; i < frame.lightCount; ++i){
        Light L = frame.lights[i];

        color += evaluatePhong(L, N, V, albedo, vWorldPos);
    }

    // TODO: emission dont work
    //color += material.emission;

    outColor = vec4(color, alpha);
}
