// Blockfall - Direct3D 11 renderer (see the header for the list of passes)
#include "renderer_d3d11.h"
#include <d3dcompiler.h>
#include <dxgi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../game/atlas.h"
#include "../game/skins.h"
#include <filesystem>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace bf {

namespace {

// ---------------------------------------------------------------- shaders
// One source for the scene (geometry) pass. Every quality setting is a number in a constant
// buffer, so switching levels needs no recompiling.
const char* kSceneShader = R"(
cbuffer Frame : register(b0) {
    row_major float4x4 viewProj;
    row_major float4x4 lightViewProj;
    float4 sunDir;
    float4 eyePos;
    float4 skyColor;
    float4 gfx;      // x smooth shading, y normal maps, z materials, w lighting
    float4 gfx2;     // x shadows, y shadow map size, z metres per shadow texel, w time
    float4 gfx3;     // x material textures loaded (0/1)
};
Texture2D shadowMap : register(t0);
SamplerComparisonState shadowSamp : register(s0);
SamplerState pointSamp : register(s1);
Texture2DArray matAlbedo : register(t2);   // world material textures, one slice per material id
Texture2DArray matNormal : register(t3);
SamplerState wrapSamp : register(s3);

struct VSIn {
    float3 pos  : POSITION;
    float3 nrm  : NORMAL;
    float3 col  : COLOR;
    float4 w0   : WORLD0;
    float4 w1   : WORLD1;
    float4 w2   : WORLD2;
    float4 w3   : WORLD3;
    float4 icol : ICOLOR;
    float4 ipar : IPARAMS;
};
struct VSOut {
    float4 pos   : SV_Position;
    float3 wpos  : TEXCOORD0;
    float3 nrm   : TEXCOORD1;
    float4 col   : COLOR;
    float2 extra : TEXCOORD2;   // x glow, y material id
};
struct PSOut {
    float4 color  : SV_Target0;
    float4 normal : SV_Target1;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    float4x4 W = float4x4(i.w0, i.w1, i.w2, i.w3);
    float4 wp = mul(float4(i.pos, 1.0), W);
    o.pos = mul(wp, viewProj);
    o.wpos = wp.xyz;
    // Normals for scaled shapes: divide by the squared scale before transforming.
    float3 s2 = float3(dot(i.w0.xyz, i.w0.xyz), dot(i.w1.xyz, i.w1.xyz), dot(i.w2.xyz, i.w2.xyz));
    o.nrm = mul(float4(i.nrm / max(s2, 1e-6), 0.0), W).xyz;
    o.col = float4(i.col, 1.0) * i.icol;
    o.extra = i.ipar.xy;
    return o;
}

float4 VSShadow(VSIn i) : SV_Position {
    float4x4 W = float4x4(i.w0, i.w1, i.w2, i.w3);
    return mul(mul(float4(i.pos, 1.0), W), lightViewProj);
}

// ---- cheap noise for the procedural normal maps
float hash3(float3 p) {
    p = frac(p * 0.3183099 + float3(0.11, 0.37, 0.73));
    p += dot(p, p.yzx + 19.19);
    return frac((p.x + p.y) * p.z);
}
float vnoise(float3 p) {
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i), n100 = hash3(i + float3(1, 0, 0)), n010 = hash3(i + float3(0, 1, 0)), n110 = hash3(i + float3(1, 1, 0));
    float n001 = hash3(i + float3(0, 0, 1)), n101 = hash3(i + float3(1, 0, 1)), n011 = hash3(i + float3(0, 1, 1)), n111 = hash3(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y), lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

// Material table: roughness, metal.
void materialOf(int mat, out float rough, out float metal) {
    rough = 0.7; metal = 0.0;
    if (mat == 1) rough = 0.62;                 // wood
    else if (mat == 2) rough = 0.85;            // stone
    else if (mat == 3) rough = 0.92;            // grass
    else if (mat == 4) rough = 0.88;            // cloth
    else if (mat == 5) { rough = 0.32; metal = 0.9; }  // metal
    else if (mat == 6) rough = 0.75;            // leaves
    else if (mat == 7) rough = 0.5;             // skin
    else if (mat == 8) rough = 0.8;             // plaster
    else if (mat == 9) rough = 0.7;             // roof tiles
    else if (mat == 10) rough = 0.9;            // bark
}

// Painted-on bumps: a height field per material. The normal is bent by its slope.
float heightField(float3 p, int mat, int level) {
    float h = 0.0;
    if (mat == 10) {                             // bark ridges
        h = vnoise(float3(p.x * 9.0, p.y * 1.5, p.z * 9.0)) * 0.7 + vnoise(p * 25.0) * 0.3;
    } else if (mat == 1) {                       // wood grain
        h = sin(p.x * 16.0 + p.z * 3.0 + vnoise(p * 2.5) * 5.0) * 0.5 + vnoise(p * 22.0) * 0.35;
    } else if (mat == 2) {                       // stone
        h = vnoise(p * 3.0) * 0.8 + vnoise(p * 11.0) * 0.35;
        if (level >= 3) h += vnoise(p * 38.0) * 0.15;
    } else if (mat == 3) {                       // grass tufts
        h = vnoise(p * 5.0) * 0.5 + vnoise(p * 26.0) * 0.5;
        if (level >= 3) h += vnoise(p * 70.0) * 0.2;
    } else if (mat == 6) {                       // leaf clumps
        h = vnoise(p * 6.0) * 0.7 + vnoise(p * 18.0) * 0.3;
    } else if (mat == 8) {                       // plaster with brick rows
        float row = floor(p.y * 3.0);
        float u = (p.x + p.z) * 1.6 + fmod(row, 2.0) * 0.5;
        float mortar = min(smoothstep(0.0, 0.08, frac(u)) * smoothstep(0.0, 0.08, 1.0 - frac(u)),
                           smoothstep(0.0, 0.12, frac(p.y * 3.0)) * smoothstep(0.0, 0.12, 1.0 - frac(p.y * 3.0)));
        h = mortar * 0.8 + vnoise(p * 30.0) * 0.2;
    } else if (mat == 9) {                       // roof tiles
        h = abs(frac(p.x * 1.2) - 0.5) * 0.8 + abs(frac(p.z * 1.2) - 0.5) * 0.4 + vnoise(p * 20.0) * 0.1;
    } else if (mat == 4 && level >= 3) {         // cloth weave
        h = vnoise(p * 60.0) * 0.08;
    }
    return h;
}

float3 bumpNormal(float3 n, float3 p, int mat, int level) {
    if (level <= 0) return n;
    float strength = level == 1 ? 0.35 : (level == 2 ? 0.7 : 0.9);
    float3 t = abs(n.y) < 0.95 ? normalize(cross(float3(0, 1, 0), n)) : float3(1, 0, 0);
    float3 b = cross(n, t);
    float e = level >= 3 ? 0.012 : 0.02;
    float h0 = heightField(p, mat, level);
    float dt = (heightField(p + t * e, mat, level) - h0) / e;
    float db = (heightField(p + b * e, mat, level) - h0) / e;
    return normalize(n - (t * dt + b * db) * strength * 0.025);
}

// ---- real textures: sampled from three sides (triplanar) so nothing needs UVs
float matTiling(int mat) {
    if (mat == 1) return 3.0;   // wood planks
    if (mat == 2) return 4.0;   // stone
    if (mat == 3) return 7.0;   // grass
    if (mat == 4) return 1.2;   // cloth
    if (mat == 5) return 3.0;   // metal panels
    if (mat == 6) return 2.5;   // leaves
    if (mat == 7) return 1.0;   // skin
    if (mat == 8) return 4.0;   // plaster
    if (mat == 9) return 3.5;   // roof tiles
    if (mat == 10) return 2.2;  // bark
    return 2.0;
}

float3 triWeights(float3 n) {
    float3 w = pow(abs(n), 4.0);
    return w / max(w.x + w.y + w.z, 1e-4);
}

// Albedo detail around 1.0 (the texture's average is mid grey).
float3 sampleAlbedo(float3 p, float3 n, int mat) {
    float s = 1.0 / matTiling(mat);
    float3 w = triWeights(n);
    float3 cx = matAlbedo.Sample(wrapSamp, float3(p.zy * s, mat)).rgb;
    float3 cy = matAlbedo.Sample(wrapSamp, float3(p.xz * s, mat)).rgb;
    float3 cz = matAlbedo.Sample(wrapSamp, float3(p.xy * s, mat)).rgb;
    return (cx * w.x + cy * w.y + cz * w.z) * 2.0;
}

// Normal from the normal map, blended from the three projections.
float3 sampleNormal(float3 p, float3 n, int mat, float strength) {
    float s = 1.0 / matTiling(mat);
    float3 w = triWeights(n);
    float3 tx = matNormal.Sample(wrapSamp, float3(p.zy * s, mat)).rgb * 2.0 - 1.0;
    float3 ty = matNormal.Sample(wrapSamp, float3(p.xz * s, mat)).rgb * 2.0 - 1.0;
    float3 tz = matNormal.Sample(wrapSamp, float3(p.xy * s, mat)).rgb * 2.0 - 1.0;
    // Each projection's tangent-space normal, put into world axes (sign follows the face).
    float3 nx = float3(tx.z * sign(n.x), tx.y, tx.x);
    float3 ny = float3(ty.x, ty.z * sign(n.y), ty.y);
    float3 nz = float3(tz.x, tz.y, tz.z * sign(n.z));
    float3 blended = nx * w.x + ny * w.y + nz * w.z;
    // Keep the base normal's direction, bend it by the map.
    float3 t = abs(n.y) < 0.95 ? normalize(cross(float3(0, 1, 0), n)) : float3(1, 0, 0);
    float3 b = cross(n, t);
    float3 bent = normalize(n + (blended - n * dot(blended, n)) * strength);
    return bent;
}
)"  // MSVC caps one string literal at ~16 KB; adjacent literals are joined, so split here.
R"(
// ---- sun shadow with PCF, softer with distance from the blocker on High
float shadowFactor(float3 wpos, float3 n, float ndl) {
    int level = (int)gfx2.x;
    if (level <= 0) return 1.0;
    float3 p = wpos + n * (0.06 + 0.12 * (1.0 - ndl));
    float4 lp = mul(float4(p, 1.0), lightViewProj);
    float2 uv = lp.xy * float2(0.5, -0.5) + 0.5;
    float d = lp.z - 0.0008;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || d > 1.0) return 1.0;
    float texel = 1.0 / gfx2.y;
    if (level == 1) return shadowMap.SampleCmpLevelZero(shadowSamp, uv, d);
    if (level == 2) {
        float s = 0.0;
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x)
                s += shadowMap.SampleCmpLevelZero(shadowSamp, uv + float2(x, y) * texel, d);
        return s / 9.0;
    }
    // High: find how far the blocker is, widen the blur with that distance (soft contact shadows).
    float blocker = 0.0; float cnt = 0.0;
    [unroll] for (int j = -1; j <= 1; j += 2)
        [unroll] for (int i = -1; i <= 1; i += 2) {
            float z = shadowMap.SampleLevel(pointSamp, uv + float2(i, j) * texel * 3.0, 0).r;
            if (z < d) { blocker += z; cnt += 1.0; }
        }
    float radius = 0.75;
    if (cnt > 0.0) {
        float distM = (d - blocker / cnt) * 400.0;   // depth units -> metres
        radius = clamp(distM * 0.12 / gfx2.z, 0.75, 5.0);
    }
    float s = 0.0;
    [unroll] for (int y = -2; y <= 2; ++y)
        [unroll] for (int x = -2; x <= 2; ++x)
            s += shadowMap.SampleCmpLevelZero(shadowSamp, uv + float2(x, y) * texel * radius * 0.5, d);
    return s / 25.0;
}

PSOut PSMain(VSOut i, bool front : SV_IsFrontFace) {
    int smooth = (int)gfx.x, nmaps = (int)gfx.y, pbr = (int)gfx.z, lighting = (int)gfx.w;
    int mat = (int)(i.extra.y + 0.5);
    float3 n = normalize(i.nrm);
    if (!front) n = -n;
    if (smooth <= 0) {
        // Flat shading: one normal per triangle, from the screen-space slope of the position.
        n = normalize(cross(ddy(i.wpos), ddx(i.wpos)));
        if (dot(n, eyePos.xyz - i.wpos) < 0.0) n = -n;
    }
    bool textured = gfx3.x > 0.5 && pbr >= 1 && mat != 0;
    if (textured && nmaps > 0) {
        n = sampleNormal(n, i.wpos, mat, nmaps == 1 ? 0.5 : (nmaps == 2 ? 0.9 : 1.2));
        if (nmaps >= 3) n = bumpNormal(n, i.wpos, mat, 1);  // plus the fine procedural grain
    } else {
        n = bumpNormal(n, i.wpos, mat, nmaps);
    }

    float rough, metal;
    materialOf(mat, rough, metal);
    float3 albedo = i.col.rgb;
    if (textured) albedo *= lerp(float3(1, 1, 1), sampleAlbedo(i.wpos, normalize(i.nrm), mat), pbr >= 2 ? 1.0 : 0.6);
    float3 v = normalize(eyePos.xyz - i.wpos);
    float3 l = -sunDir.xyz;
    float ndl = saturate(dot(n, l));
    float ndv = saturate(dot(n, v));
    float3 sunCol = float3(1.0, 0.95, 0.85) * 1.1;
    float shadow = shadowFactor(i.wpos, n, ndl);

    // Ambient: flat, or sky/ground hemisphere.
    float3 ambient;
    if (lighting <= 0) ambient = float3(0.5, 0.5, 0.5);
    else ambient = lerp(float3(0.40, 0.38, 0.34), float3(0.58, 0.70, 0.88), n.y * 0.5 + 0.5) * (lighting >= 3 ? 1.08 : 1.0);
    ambient *= 0.62;

    float3 c = albedo * (1.0 - metal * (pbr >= 1 ? 0.8 : 0.0)) * (ambient + ndl * sunCol * shadow);

    // Specular.
    if (pbr >= 1 && ndl > 0.0) {
        float3 h = normalize(l + v);
        float ndh = saturate(dot(n, h));
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
        float3 spec = 0.0;
        if (pbr == 1) {
            spec = F0 * pow(ndh, lerp(64.0, 4.0, rough)) * (1.0 - rough) * 0.8;
        } else {
            float a = max(rough * rough, 0.02), a2 = a * a;
            float dd = ndh * ndh * (a2 - 1.0) + 1.0;
            float D = a2 / (3.14159 * dd * dd);
            float k = (rough + 1.0) * (rough + 1.0) / 8.0;
            float G = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));
            float vdh = saturate(dot(v, h));
            float3 F = pbr >= 3 ? F0 + (1.0 - F0) * pow(1.0 - vdh, 5.0) : F0;
            spec = D * G * F / max(4.0 * ndl * ndv, 0.05);
        }
        c += spec * sunCol * ndl * shadow;
    }
    if (pbr >= 3) {
        // Sky reflection on shiny or metallic things, strongest at grazing angles.
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
        float3 F = F0 + (1.0 - F0) * pow(1.0 - ndv, 5.0);
        float3 r = reflect(-v, n);
        float3 env = lerp(float3(0.36, 0.42, 0.30), skyColor.rgb, saturate(r.y * 0.5 + 0.5));
        c += env * F * (1.0 - rough) * (0.25 + 0.75 * metal);
    }
    if (smooth >= 2) {
        // Soft rim light so round shapes read against the background.
        float rim = pow(1.0 - ndv, 3.0);
        c += rim * (smooth >= 3 ? 0.22 : 0.16) * ambient * (mat == 7 ? 1.6 : 1.0);
    }
    c += albedo * i.extra.x * 0.7;

    PSOut o;
    o.color = float4(c, i.col.a);
    o.normal = float4(n * 0.5 + 0.5, rough);
    return o;
}

)"  // a third literal: the character shaders (each literal must stay under MSVC's ~16 KB)
R"(
// ---- skinned characters: each vertex is moved by up to 4 bones. Two looks: Stylized (cartoon
// steps + outline) and Realistic (PBR). The shared detail atlas adds baked normals, roughness,
// metalness, AO and a zone per texel; outfit pieces, hair and face parts take their colour from
// the zone palette, the body from its painted skin texture.
cbuffer Bones : register(b1) { row_major float4x4 bones[48]; };
cbuffer Char : register(b2) {
    row_major float4x4 charWorld;
    float4 charTint;
    float4 charAccent;
    float4 charParams;   // x glow, y hurt flash, z accent strength, w outline width
    float4 charFlags;    // x mesh kind (0 body, 1 painted mesh, 2 atlas mesh), y face parts drawn, z style (1 realistic), w atlas loaded
    float4 zoneColor[16];
    float4 zoneMat[16];  // x roughness, y metalness (each -1 = use the atlas)
    float4 charFx;       // emission: x trim/accent paint and the accent zone, y eyes and goggle lenses
};
Texture2D skinTex : register(t1);
Texture2D charNormal : register(t4);
Texture2D charMaterial : register(t5);
SamplerState skinSamp : register(s2);

// Zones (tools/character/meshutil.py).
static const int Z_CUT = 0, Z_ACCENT = 3, Z_HAIR = 6, Z_SKIN = 7, Z_EYE = 8, Z_LIPS = 10, Z_BODY = 11, Z_LENS = 12, Z_BODY_SKIN = 13;

struct VSSkinIn {
    float3 pos     : POSITION;
    float3 nrm     : NORMAL;
    float2 uv      : TEXCOORD0;
    float2 uv2     : TEXCOORD1;
    uint4  joints  : JOINTS;
    float4 weights : WEIGHTS;
};
struct VSSkinOut {
    float4 pos  : SV_Position;
    float3 wpos : TEXCOORD0;
    float3 nrm  : TEXCOORD1;
    float2 uv   : TEXCOORD2;
    float2 uv2  : TEXCOORD3;
};

float4x4 skinMatrix(VSSkinIn i) {
    return bones[i.joints.x] * i.weights.x + bones[i.joints.y] * i.weights.y +
           bones[i.joints.z] * i.weights.z + bones[i.joints.w] * i.weights.w;
}

VSSkinOut VSSkin(VSSkinIn i) {
    VSSkinOut o;
    float4x4 S = mul(skinMatrix(i), charWorld);
    float4 wp = mul(float4(i.pos, 1.0), S);
    o.pos = mul(wp, viewProj);
    o.wpos = wp.xyz;
    o.nrm = normalize(mul(float4(i.nrm, 0.0), S).xyz);
    o.uv = i.uv;
    o.uv2 = i.uv2;
    return o;
}

// The outline: the mesh again, pushed out along its normals and drawn with front faces culled,
// so only a thin rim shows around the silhouette.
VSSkinOut VSSkinOutline(VSSkinIn i) {
    VSSkinOut o;
    float4x4 S = mul(skinMatrix(i), charWorld);
    float4 wp = mul(float4(i.pos, 1.0), S);
    float3 n = normalize(mul(float4(i.nrm, 0.0), S).xyz);
    float dist = length(eyePos.xyz - wp.xyz);
    wp.xyz += n * charParams.w * (1.0 + dist * 0.03);
    o.pos = mul(wp, viewProj);
    o.wpos = wp.xyz;
    o.nrm = n;
    o.uv = i.uv;
    o.uv2 = i.uv2;
    return o;
}

float4 VSSkinShadow(VSSkinIn i) : SV_Position {
    float4x4 S = mul(skinMatrix(i), charWorld);
    return mul(mul(float4(i.pos, 1.0), S), lightViewProj);
}

// Hair cards in the shadow map: the see-through gaps must not cast shadows.
struct VSShadowUV { float4 pos : SV_Position; float2 uv2 : TEXCOORD0; };
VSShadowUV VSSkinShadowUV(VSSkinIn i) {
    VSShadowUV o;
    float4x4 S = mul(skinMatrix(i), charWorld);
    o.pos = mul(mul(float4(i.pos, 1.0), S), lightViewProj);
    o.uv2 = i.uv2;
    return o;
}

int atlasZone(float2 uv) {
    uint w, h;
    charMaterial.GetDimensions(w, h);
    // An exact texel (no filtering, no mips): averaging two zone numbers would make a third.
    return (int)(charMaterial.Load(int3(int2(frac(uv) * float2(w, h)), 0)).a * 255.0 / 16.0);
}

void PSCharShadowClip(VSShadowUV i) {
    if (charFlags.w > 0.5 && atlasZone(i.uv2) == Z_CUT) discard;
}

// Normal mapping without stored tangents: the tangent frame comes from how the position and
// the UVs change across the pixel ("cotangent frame").
float3 perturbNormal(float3 N, float3 p, float2 uv, float3 tn, out float3 B) {
    float3 dp1 = ddx(p), dp2 = ddy(p);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    T *= invmax;
    B *= invmax;
    return normalize(T * tn.x + B * tn.y + N * tn.z);
}

struct CharSurf {
    float3 albedo, n, ngeo, strand, emit;
    float rough, metal, ao;
    int zone;
};

CharSurf charSurface(VSSkinOut i, bool front) {
    CharSurf s;
    float3 n = normalize(i.nrm);
    if (!front) n = -n;
    s.ngeo = n;
    s.n = n;
    s.strand = float3(0, -1, 0);
    s.rough = 0.7;
    s.metal = 0.0;
    s.ao = 1.0;
    s.zone = Z_BODY;
    s.emit = float3(0, 0, 0);
    int kind = (int)(charFlags.x + 0.5);
    if (charFlags.w > 0.5) {
        s.zone = atlasZone(i.uv2);
        if (s.zone == Z_CUT) discard;
        float4 m = charMaterial.Sample(skinSamp, i.uv2);
        s.rough = m.r;
        s.metal = m.g;
        s.ao = m.b;
        float3 tn = charNormal.Sample(skinSamp, i.uv2).xyz * 2.0 - 1.0;
        tn.y = -tn.y;   // baked with +v up the image; the game's v runs down it
        float3 B;
        s.n = perturbNormal(n, i.wpos, i.uv2, tn, B);
        s.strand = normalize(B);
    }
    bool painted = kind != 2 && (s.zone == Z_BODY || s.zone == Z_BODY_SKIN || (s.zone == Z_SKIN && charFlags.y < 0.5));
    if (painted) {
        float4 tex = skinTex.Sample(skinSamp, i.uv);
        s.albedo = tex.rgb * charTint.rgb;
        s.albedo = lerp(s.albedo, charAccent.rgb, tex.a * charParams.z);   // team trim
        // Emission mask: the painted trim (texture alpha) glows on FX skins (energy cracks,
        // circuit lines, piping).
        s.emit = s.albedo * tex.a * charFx.x;
    } else {
        s.albedo = zoneColor[s.zone].rgb;
        if (s.zone == Z_ACCENT) {
            s.albedo = lerp(s.albedo, charAccent.rgb, charParams.z);
            s.emit = s.albedo * charFx.x;
        }
        if (s.zone == Z_EYE || s.zone == Z_LENS) s.emit = lerp(float3(1, 1, 1), zoneColor[Z_ACCENT].rgb, 0.8) * charFx.y;
        float4 zm = zoneMat[s.zone];
        if (zm.x >= 0.0) s.rough = saturate(zm.x + (s.rough - 0.5) * 0.4);
        if (zm.y >= 0.0) s.metal = zm.y;
    }
    s.albedo = lerp(s.albedo, float3(1, 1, 1), charParams.y * 0.6);   // hurt flash
    return s;
}

PSOut PSChar(VSSkinOut i, bool front : SV_IsFrontFace) {
    int lighting = (int)gfx.w, pbr = (int)gfx.z, smooth = (int)gfx.x;
    CharSurf s = charSurface(i, front);
    float3 v = normalize(eyePos.xyz - i.wpos);
    float3 l = -sunDir.xyz;
    float3 sunCol = float3(1.0, 0.95, 0.85);
    float shadow = shadowFactor(i.wpos, s.ngeo, saturate(dot(s.ngeo, l)));
    float3 ambient;
    if (lighting <= 0) ambient = float3(0.5, 0.5, 0.5);
    else ambient = lerp(float3(0.40, 0.38, 0.34), float3(0.58, 0.70, 0.88), s.n.y * 0.5 + 0.5) * (lighting >= 3 ? 1.08 : 1.0);
    float3 c;

    if (charFlags.z < 0.5) {
        // Stylized: two soft light steps (Smooth Shading Off = hard steps). The baked detail
        // shows inside the steps, at half strength so the cartoon look stays.
        float3 n = normalize(lerp(s.ngeo, s.n, 0.5));
        float lit = dot(n, l) * shadow;
        float soft = smooth > 0 ? 0.08 : 0.01;
        float band = smoothstep(0.02 - soft, 0.02 + soft, lit) * 0.6 + smoothstep(0.45 - soft, 0.45 + soft, lit) * 0.4;
        ambient *= 0.68 * lerp(1.0, s.ao, 0.7);
        c = s.albedo * (ambient + sunCol * band * 1.05);
        if (pbr >= 1) {
            float3 h = normalize(l + v);
            float shine = 1.0 - s.rough;
            float spec = smoothstep(0.93 - shine * 0.05, 0.97, saturate(dot(n, h))) * (0.08 + 0.3 * shine) * band;
            c += spec * sunCol * lerp(float3(1, 1, 1), s.albedo, s.metal);
        }
        float rim = pow(1.0 - saturate(dot(n, v)), 3.0) * smoothstep(0.1, 0.4, lit + 0.2);
        c += rim * 0.3 * (ambient + sunCol * 0.3);
    } else {
        // Realistic: physically based. Skin lets light wrap round and glow red at the shadow
        // edge (a cheap stand-in for light scattering under the skin); hair gets a highlight
        // that runs across the strands.
        float3 n = s.n;
        float ndl = dot(n, l);
        float ndv = max(dot(n, v), 1e-3);
        bool skin = s.zone == Z_SKIN || s.zone == Z_BODY_SKIN || s.zone == Z_LIPS;
        float diffuseTerm = skin ? saturate((ndl + 0.45) / 1.45) : saturate(ndl);
        ambient *= 0.62 * s.ao;
        c = s.albedo * (1.0 - s.metal) * (ambient + sunCol * diffuseTerm * shadow * 1.1);
        if (skin) c += s.albedo * float3(0.30, 0.07, 0.03) * saturate(1.0 - abs(ndl) * 2.2) * shadow;
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), s.albedo, s.metal);
        float3 h = normalize(l + v);
        if (s.zone == Z_HAIR) {
            float th = dot(s.strand, h);
            float kk = pow(sqrt(saturate(1.0 - th * th)), 70.0) * 0.35 + pow(sqrt(saturate(1.0 - th * th)), 12.0) * 0.08;
            c += kk * sunCol * lerp(float3(1, 1, 1), s.albedo, 0.5) * shadow * saturate(ndl + 0.3);
        } else if (pbr >= 1 && ndl > 0.0) {
            float ndh = saturate(dot(n, h));
            float a = max(s.rough * s.rough, 0.02), a2 = a * a;
            float dd = ndh * ndh * (a2 - 1.0) + 1.0;
            float D = a2 / (3.14159 * dd * dd);
            float k = (s.rough + 1.0) * (s.rough + 1.0) / 8.0;
            float G = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));
            float3 F = F0 + (1.0 - F0) * pow(1.0 - saturate(dot(v, h)), 5.0);
            float3 spec = D * G * F / max(4.0 * ndl * ndv, 0.05);
            c += min(spec, 8.0) * sunCol * ndl * shadow;
        }
        // Sky and ground reflected in shiny and metal surfaces, strongest at grazing angles.
        float3 F = F0 + (1.0 - F0) * pow(1.0 - ndv, 5.0);
        float3 r = reflect(-v, n);
        float3 env = lerp(float3(0.36, 0.42, 0.30), skyColor.rgb, saturate(r.y * 0.5 + 0.5));
        c += env * F * (1.0 - s.rough) * (0.3 + 0.7 * s.metal) * s.ao;
        c += pow(1.0 - ndv, 4.0) * 0.12 * ambient;
    }
    c += s.albedo * charParams.x * 0.7;
    c += s.emit * 1.8;   // emission FX: unaffected by light or shadow (Post-processing blooms it)

    PSOut o;
    o.color = float4(c, 1.0);
    o.normal = float4(s.n * 0.5 + 0.5, s.rough);
    return o;
}

PSOut PSOutline(VSSkinOut i) {
    PSOut o;
    o.color = float4(0.03, 0.03, 0.04, 1.0);
    o.normal = float4(normalize(i.nrm) * 0.5 + 0.5, 1.0);
    return o;
}
)";

// Full-screen passes.
const char* kPostShader = R"(
cbuffer Post : register(b0) {
    row_major float4x4 invViewProj;
    row_major float4x4 prevViewProj;
    float4 p0;   // x width, y height, z near, w far
    float4 p1;   // x lighting level, y post level, z aa level, w time
    float4 p2;   // x fog start, y fog end, z dof focus (m, 0 = off), w blur direction (0 x, 1 y)
    float4 sky;
    float4 eye;
    float4 p3;   // x taa blend, y sharpen, z jitter x, w jitter y (uv)
};
Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Texture2D t2 : register(t2);
SamplerState linS : register(s0);
SamplerState pointS : register(s1);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };
VSOut VSFull(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

float linearDepth(float d) { return p0.z * p0.w / (p0.w - d * (p0.w - p0.z)); }
float3 worldAt(float2 uv, float d) {
    float4 ndc = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
    float4 w = mul(ndc, invViewProj);
    return w.xyz / w.w;
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// ---- lighting pass: ambient occlusion (Medium) and colour bounce (High)
float4 PSLight(VSOut i) : SV_Target {
    float4 col = t0.Sample(pointS, i.uv);
    int level = (int)p1.x;
    if (level < 2) return col;
    float d = t2.Sample(pointS, i.uv).r;
    if (d >= 0.9999) return col;
    float3 n = normalize(t1.Sample(pointS, i.uv).xyz * 2.0 - 1.0);
    float3 pos = worldAt(i.uv, d);
    float dist = linearDepth(d);
    float radiusPx = clamp(220.0 / dist, 4.0, 48.0) / p0.y;   // ~1.4 m in screen space
    float occ = 0.0; float3 bounce = 0.0; float bw = 0.0;
    // Interleaved gradient noise: fine-grained, and it drifts each frame so temporal AA averages it.
    float2 pix = floor(i.uv * p0.xy);
    float ign = frac(52.9829189 * frac(0.06711056 * pix.x + 0.00583715 * pix.y));
    float ang = (ign + (p1.z >= 2.0 ? frac(p1.w * 7.3) : 0.0)) * 6.2832;
    [unroll] for (int k = 0; k < 10; ++k) {
        float a = ang + k * 0.6283;
        float r = radiusPx * (0.25 + 0.75 * frac(k * 0.618 + 0.3));
        float2 suv = i.uv + float2(cos(a), sin(a) * (p0.x / p0.y)) * r;
        suv = clamp(suv, 0.001, 0.999);
        float sd = t2.Sample(pointS, suv).r;
        float3 sp = worldAt(suv, sd);
        float3 dv = sp - pos;
        float len = length(dv);
        float f = saturate(dot(n, dv / max(len, 1e-4)) - 0.12);
        float range = saturate(1.0 - len / 1.6);
        occ += f * range;
        if (level >= 3) {
            float3 sc = t0.Sample(linS, suv).rgb;
            bounce += sc * f * range;
            bw += f * range;
        }
    }
    float ao = 1.0 - saturate(occ / 10.0 * 1.3);
    ao = lerp(1.0, ao, 0.8);
    float3 c = col.rgb * ao;
    if (level >= 3 && bw > 0.0) c += (bounce / 10.0) * col.rgb * 0.45;
    return float4(c, col.a);
}

float4 PSDown(VSOut i) : SV_Target { return t0.Sample(linS, i.uv); }

float4 PSBlur(VSOut i) : SV_Target {
    float2 dir = p2.w < 0.5 ? float2(1.0 / p0.x, 0.0) : float2(0.0, 1.0 / p0.y);
    float w[5] = {0.227, 0.195, 0.122, 0.054, 0.016};
    float4 c = t0.Sample(linS, i.uv) * w[0];
    [unroll] for (int k = 1; k < 5; ++k) {
        c += t0.Sample(linS, i.uv + dir * k * 1.5) * w[k];
        c += t0.Sample(linS, i.uv - dir * k * 1.5) * w[k];
    }
    return c;
}

float3 aces(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// ---- compose: fog, bloom, tonemap, grading, depth of field, vignette
float4 PSCompose(VSOut i) : SV_Target {
    int level = (int)p1.y;
    float3 c = t0.Sample(pointS, i.uv).rgb;
    if (level <= 0) return float4(saturate(c), 1.0);
    float d = t2.Sample(pointS, i.uv).r;
    float dist = d >= 0.9999 ? 1e5 : length(worldAt(i.uv, d) - eye.xyz);
    float3 blur = t1.Sample(linS, i.uv).rgb;
    if (level >= 3 && p2.z > 0.0) {
        // Depth of field while aiming: things far behind the focus distance go soft.
        float coc = saturate((dist - p2.z) / 45.0) * 0.85 + saturate((p2.z * 0.35 - dist) / 2.0) * 0.6;
        c = lerp(c, blur, coc);
    }
    if (level >= 2) {
        float3 bright = max(blur - 0.75, 0.0);
        c += bright * 0.7;
    }
    float f = saturate((dist - p2.x) / (p2.y - p2.x));
    c = lerp(c, sky.rgb, f * f);
    c = aces(c * 0.95) * 1.06;
    if (level >= 2) {
        // Gentle grade: a bit more contrast, warm highlights, cool shadows.
        float l = luma(c);
        c = lerp(float3(l, l, l), c, 1.1);
        c = (c - 0.5) * 1.06 + 0.5;
        c += (float3(0.03, 0.012, -0.02) * l + float3(-0.01, 0.0, 0.025) * (1.0 - l)) * 0.8;
    }
    if (level >= 3) {
        float2 q = i.uv - 0.5;
        c *= 1.0 - dot(q, q) * 0.45;
    }
    return float4(saturate(c), 1.0);
}

// ---- FXAA (compact)
float4 PSFxaa(VSOut i) : SV_Target {
    float2 px = 1.0 / p0.xy;
    float3 rgbM = t0.Sample(pointS, i.uv).rgb;
    float lM = luma(rgbM);
    float lNW = luma(t0.Sample(pointS, i.uv + float2(-1, -1) * px).rgb);
    float lNE = luma(t0.Sample(pointS, i.uv + float2(1, -1) * px).rgb);
    float lSW = luma(t0.Sample(pointS, i.uv + float2(-1, 1) * px).rgb);
    float lSE = luma(t0.Sample(pointS, i.uv + float2(1, 1) * px).rgb);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.03, lMax * 0.12)) return float4(rgbM, 1.0);
    float2 dir = float2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcp, -8.0, 8.0) * px;
    float3 rgbA = 0.5 * (t0.Sample(linS, i.uv + dir * (1.0 / 3.0 - 0.5)).rgb + t0.Sample(linS, i.uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (t0.Sample(linS, i.uv + dir * -0.5).rgb + t0.Sample(linS, i.uv + dir * 0.5).rgb);
    float lB = luma(rgbB);
    if (lB < lMin || lB > lMax) return float4(rgbA, 1.0);
    return float4(rgbB, 1.0);
}

// ---- temporal AA: blend with last frame, reprojected through the depth buffer
float3 toYCoCg(float3 c) { return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
float3 fromYCoCg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

float4 PSTaa(VSOut i) : SV_Target {
    float2 px = 1.0 / p0.xy;
    float2 uvJ = i.uv - p3.zw;   // undo this frame's jitter when reading the current image
    float3 cur = t0.Sample(linS, uvJ).rgb;
    // Neighbourhood statistics in YCoCg: the history may only be as different from the
    // current frame as its neighbours are (variance clipping), which kills ghosting without
    // the colour fringes a plain RGB box clamp gives on dark edges.
    float3 m1 = 0.0, m2 = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x) {
            float3 s = toYCoCg(t0.Sample(pointS, uvJ + float2(x, y) * px).rgb);
            m1 += s; m2 += s * s;
        }
    float3 mean = m1 / 9.0;
    float3 sigma = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    float3 mn = mean - 1.25 * sigma, mx = mean + 1.25 * sigma;

    float d = t2.Sample(pointS, i.uv).r;
    float3 world = worldAt(i.uv, d);
    float4 prev = mul(float4(world, 1.0), prevViewProj);
    float2 puv = (prev.xy / prev.w) * float2(0.5, -0.5) + 0.5;
    float blend = p3.x;
    if (prev.w <= 0.0 || puv.x < 0.0 || puv.x > 1.0 || puv.y < 0.0 || puv.y > 1.0) blend = 1.0;
    float3 hist = toYCoCg(t1.Sample(linS, puv).rgb);
    // Clip toward the mean (not just clamp) so edges stay crisp.
    float3 dir = hist - mean;
    float3 lim = max(abs(dir) / max(mx - mean, 1e-4), 1.0);
    float t = 1.0 / max(max(lim.x, lim.y), lim.z);
    hist = mean + dir * t;
    float3 c = lerp(hist, toYCoCg(cur), blend);
    if (p3.y > 0.0) {
        // Sharpen the brightness only; colours are left alone (no rainbow fringes).
        float lb = (luma(t0.Sample(linS, uvJ + float2(px.x, 0)).rgb) + luma(t0.Sample(linS, uvJ - float2(px.x, 0)).rgb) +
                    luma(t0.Sample(linS, uvJ + float2(0, px.y)).rgb) + luma(t0.Sample(linS, uvJ - float2(0, px.y)).rgb)) * 0.25;
        float lc = luma(fromYCoCg(c));
        float ls = max(lc + (lc - lb) * p3.y, 0.0);
        c.x *= (lc > 1e-4) ? ls / lc : 1.0;
    }
    return float4(saturate(fromYCoCg(c)), 1.0);
}

// Supersampling resolve: the render is 2x the window in both directions; average each 2x2.
float4 PSDownsample(VSOut i) : SV_Target {
    float2 px = 1.0 / p0.xy;   // p0 = the big render size
    float3 c = t0.Sample(linS, i.uv + float2(-0.5, -0.5) * px).rgb + t0.Sample(linS, i.uv + float2(0.5, -0.5) * px).rgb +
               t0.Sample(linS, i.uv + float2(-0.5, 0.5) * px).rgb + t0.Sample(linS, i.uv + float2(0.5, 0.5) * px).rgb;
    return float4(c * 0.25, 1.0);
}

float4 PSCopy(VSOut i) : SV_Target { return t0.Sample(pointS, i.uv); }
)";

const char* kHudShader = R"(
cbuffer Hud : register(b0) { float4 screen; };
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; float4 col : COLOR; };
VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos.x / screen.x * 2.0 - 1.0, 1.0 - i.pos.y / screen.y * 2.0, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
float4 PSMain(VSOut i) : SV_Target { return tex.Sample(samp, i.uv) * i.col; }
)";

struct FrameCB {
    Mat4 viewProj = Mat4::identity(), lightViewProj = Mat4::identity();
    Vec4 sunDir, eyePos, sky, gfx, gfx2, gfx3;
};
struct PostCB {
    Mat4 invViewProj = Mat4::identity(), prevViewProj = Mat4::identity();
    Vec4 p0, p1, p2, sky, eye, p3;
};
struct HudVertex {
    float x, y, u, v;
    Vec4 c;
};
struct CharCB {
    Mat4 world = Mat4::identity();
    Vec4 tint, accent, params;
    Vec4 flags;            // x mesh kind, y face parts drawn, z style, w atlas loaded
    Vec4 zoneColor[16];
    Vec4 zoneMat[16];      // x roughness, y metalness (-1 = from the atlas)
    Vec4 fx;               // x trim/accent emission, y eye/lens emission (glow strength)
};

// Atlas zones (tools/character/meshutil.py; keep the two lists in step).
enum CharZone { ZONE_CUT = 0, ZONE_MAIN, ZONE_SECOND, ZONE_ACCENT, ZONE_LEATHER, ZONE_METAL, ZONE_HAIR, ZONE_SKIN, ZONE_EYE,
                ZONE_DARK, ZONE_LIPS, ZONE_BODY, ZONE_LENS, ZONE_BODY_SKIN };
enum CharMeshKind { KIND_BODY = 0, KIND_PAINTED = 1, KIND_ATLAS = 2 };

bool compile(const char* src, const char* entry, const char* target, ID3DBlob** out, std::string& error) {
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    if (FAILED(hr)) {
        error = std::string("Shader compile failed (") + entry + "): ";
        if (err) error += (const char*)err->GetBufferPointer();
        safeRelease(err);
        return false;
    }
    safeRelease(err);
    return true;
}

float halton(unsigned i, unsigned base) {
    float f = 1, r = 0;
    while (i > 0) {
        f /= base;
        r += f * (i % base);
        i /= base;
    }
    return r;
}

// Renders the text cells of the atlas with a smooth, bold monospace system font.
// Keeps the built-in pixel font if Windows can't give us one.
void drawSystemFont(std::vector<uint32_t>& atlas) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ATLAS_W;
    bmi.bmiHeader.biHeight = -ATLAS_TEXT_H;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, (size_t)ATLAS_W * ATLAS_TEXT_H * 4);
    // A clean bold game font: the first of these that Windows really has.
    static const char* wanted[] = {"Bahnschrift", "Segoe UI", "Arial", "Tahoma", "Verdana", "Consolas"};
    HFONT font = nullptr;
    for (const char* name : wanted) {
        HFONT f = CreateFontA(-29, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
        if (!f) continue;
        HGDIOBJ prev = SelectObject(dc, f);
        char face[64] = {};
        GetTextFaceA(dc, sizeof face, face);
        if (lstrcmpiA(face, name) == 0) { font = f; break; }
        SelectObject(dc, prev);
        DeleteObject(f);
    }
    if (!font) {
        // None of those by name: take whatever TrueType font Windows substitutes for Arial.
        HFONT f = CreateFontA(-29, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
        if (f) {
            HGDIOBJ prev = SelectObject(dc, f);
            TEXTMETRICA tmc;
            GetTextMetricsA(dc, &tmc);
            if (tmc.tmPitchAndFamily & TMPF_TRUETYPE) font = f;
            else { SelectObject(dc, prev); DeleteObject(f); }
        }
    }
    if (!font) {
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
        return;  // keep the built-in pixel font
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    float adv[128];
    for (float& a : adv) a = CHAR_ADVANCE;
    {
        SIZE sp;
        char space = ' ';
        GetTextExtentPoint32A(dc, &space, 1, &sp);
        adv[' '] = std::max(2.0f, sp.cx / 4.0f);
    }
    for (int c = 33; c < 127; ++c) {
        if (c >= 'a' && c <= 'z') continue;  // lowercase shares the uppercase cells
        int cx, cy;
        textCellOrigin((char)c, cx, cy);
        char ch = (char)c;
        SIZE sz;
        GetTextExtentPoint32A(dc, &ch, 1, &sz);
        RECT clip = {cx, cy, cx + TEXT_CELL_W, cy + TEXT_CELL_H};
        int x = cx + (TEXT_CELL_W - (int)sz.cx) / 2;
        int y = cy + 28 - (int)tm.tmAscent;  // baseline near the bottom of the cell
        ExtTextOutA(dc, x, y, ETO_CLIPPED, &clip, &ch, 1, nullptr);
        adv[c] = std::min((float)TEXT_CELL_W, (float)sz.cx + 1.0f) / 4.0f;  // + a little letter spacing
    }
    GdiFlush();
    // Copy coverage into the atlas (only if the font actually drew something).
    const uint32_t* px = (const uint32_t*)bits;
    size_t lit = 0;
    for (size_t i = 0; i < (size_t)ATLAS_W * ATLAS_TEXT_H; ++i) lit += (px[i] & 0xFF) > 0;
    if (lit > 2000) {
        for (int c = 33; c < 127; ++c) {
            if (c >= 'a' && c <= 'z') continue;
            int cx, cy;
            textCellOrigin((char)c, cx, cy);
            for (int y = 0; y < TEXT_CELL_H; ++y)
                for (int x = 0; x < TEXT_CELL_W; ++x) {
                    size_t i = (size_t)(cy + y) * ATLAS_W + cx + x;
                    uint32_t a = px[i] & 0xFF;
                    atlas[i] = (a << 24) | 0x00FFFFFFu;
                }
        }
        for (int c = 'a'; c <= 'z'; ++c) adv[c] = adv[c - 32];
        setGlyphAdvances(adv);
    }
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
}

}  // namespace

// ---------------------------------------------------------------- init
bool RendererD3D11::init(HWND hwnd, int width, int height, std::string& error) {
    hwnd_ = hwnd;
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = width_;
    sd.BufferDesc.Height = height_;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 3,
                                               D3D11_SDK_VERSION, &sd, &swap_, &dev_, &got, &ctx_);
    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.BufferCount = 1;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 3,
                                           D3D11_SDK_VERSION, &sd, &swap_, &dev_, &got, &ctx_);
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 3,
                                           D3D11_SDK_VERSION, &sd, &swap_, &dev_, &got, &ctx_);
    }
    if (FAILED(hr)) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "Could not create a Direct3D 11 device (HRESULT 0x%08lX).", (unsigned long)hr);
        error = buf;
        return false;
    }
    {
        IDXGIDevice* dxgiDev = nullptr;
        if (SUCCEEDED(dev_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev))) {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgiDev->GetAdapter(&adapter))) {
                DXGI_ADAPTER_DESC ad;
                if (SUCCEEDED(adapter->GetDesc(&ad))) {
                    char name[128];
                    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof name, nullptr, nullptr);
                    adapterName_ = name;
                }
                safeRelease(adapter);
            }
            safeRelease(dxgiDev);
        }
        IDXGIFactory* factory = nullptr;
        if (SUCCEEDED(swap_->GetParent(__uuidof(IDXGIFactory), (void**)&factory))) {
            factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
            safeRelease(factory);
        }
    }

    if (!createTargets(error)) return false;
    if (!compileShaders(error, got >= D3D_FEATURE_LEVEL_11_0)) return false;

    D3D11_BUFFER_DESC cb = {};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb.ByteWidth = sizeof(FrameCB);
    dev_->CreateBuffer(&cb, nullptr, &cbFrame_);
    cb.ByteWidth = sizeof(PostCB);
    dev_->CreateBuffer(&cb, nullptr, &cbPost_);
    cb.ByteWidth = 16;
    dev_->CreateBuffer(&cb, nullptr, &cbHud_);
    cb.ByteWidth = sizeof(Mat4) * MAX_BONES;
    dev_->CreateBuffer(&cb, nullptr, &cbBones_);
    cb.ByteWidth = sizeof(CharCB);
    dev_->CreateBuffer(&cb, nullptr, &cbChar_);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // two-sided geometry (cones, thin pieces)
    rd.DepthClipEnable = TRUE;
    dev_->CreateRasterizerState(&rd, &rsSolid_);
    rd.DepthBias = 24;
    rd.SlopeScaledDepthBias = 2.0f;
    rd.DepthBiasClamp = 0.01f;
    dev_->CreateRasterizerState(&rd, &rsShadow_);
    rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_FRONT;  // outlines: only the back of the pushed-out shell shows
    rd.DepthClipEnable = TRUE;
    dev_->CreateRasterizerState(&rd, &rsCullFront_);

    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev_->CreateDepthStencilState(&dd, &dsOpaque_);
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dev_->CreateDepthStencilState(&dd, &dsTransparent_);
    dd.DepthEnable = FALSE;
    dev_->CreateDepthStencilState(&dd, &dsOff_);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    bd.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev_->CreateBlendState(&bd, &bsOpaque_);
    bd.IndependentBlendEnable = TRUE;
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[1].RenderTargetWriteMask = 0;  // see-through things don't write normals
    dev_->CreateBlendState(&bd, &bsAlpha_);

    D3D11_SAMPLER_DESC smp = {};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    dev_->CreateSamplerState(&smp, &sampLinear_);
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    dev_->CreateSamplerState(&smp, &sampPoint_);
    smp.Filter = D3D11_FILTER_ANISOTROPIC;
    smp.MaxAnisotropy = 8;
    dev_->CreateSamplerState(&smp, &sampSkin_);
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    smp.MaxAnisotropy = 16;
    dev_->CreateSamplerState(&smp, &sampWrap_);
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxAnisotropy = 1;
    smp.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    smp.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    smp.BorderColor[0] = smp.BorderColor[1] = smp.BorderColor[2] = smp.BorderColor[3] = 1.0f;
    dev_->CreateSamplerState(&smp, &sampShadow_);

    // HUD atlas: icons + text, with a smooth system font when one is available.
    std::vector<uint32_t> atlas = buildAtlas();
    drawSystemFont(atlas);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = ATLAS_W;
    td.Height = ATLAS_H;
    td.MipLevels = 0;  // mipmaps keep small text smooth
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    dev_->CreateTexture2D(&td, nullptr, &atlasTex_);
    if (atlasTex_) {
        ctx_->UpdateSubresource(atlasTex_, 0, nullptr, atlas.data(), ATLAS_W * 4, 0);
        dev_->CreateShaderResourceView(atlasTex_, nullptr, &atlasSrv_);
        if (atlasSrv_) ctx_->GenerateMips(atlasSrv_);
    }

    // Meshes at every level of detail.
    std::vector<MeshVertex> v;
    std::vector<uint32_t> idx;
    makeCubeMesh(v, idx);
    if (!createMesh(v, idx, meshes_[MESH_CUBE][0])) { error = "Failed to create cube mesh."; return false; }
    makeConeQuadMesh(v, idx);
    if (!createMesh(v, idx, meshes_[MESH_CONE_QUAD][0])) { error = "Failed to create cone mesh."; return false; }
    makeTriMesh(v, idx);
    if (!createMesh(v, idx, meshes_[MESH_TRI][0])) { error = "Failed to create triangle mesh."; return false; }
    for (int lod = 0; lod < MESH_LODS; ++lod) {
        makeSphereMesh(v, idx, lod);
        if (!createMesh(v, idx, meshes_[MESH_SPHERE][lod])) { error = "Failed to create sphere mesh."; return false; }
        makeCylinderMesh(v, idx, lod);
        if (!createMesh(v, idx, meshes_[MESH_CYLINDER][lod])) { error = "Failed to create cylinder mesh."; return false; }
        makeRockMesh(v, idx, lod);
        if (!createMesh(v, idx, meshes_[MESH_ROCK][lod])) { error = "Failed to create rock mesh."; return false; }
    }
    setGraphics(gfx_);

    if (!rsSolid_ || !dsOpaque_ || !bsAlpha_ || !sampLinear_ || !sampShadow_ || !atlasSrv_ || !cbFrame_ || !cbPost_ || !cbHud_) {
        error = "Failed to create Direct3D 11 states.";
        return false;
    }
    return true;
}

bool RendererD3D11::compileShaders(std::string& error, bool sm5) {
    const char* vsT = sm5 ? "vs_5_0" : "vs_4_0";
    const char* psT = sm5 ? "ps_5_0" : "ps_4_0";
    ID3DBlob *vsb = nullptr, *psb = nullptr;
    HRESULT hr;

    if (!compile(kSceneShader, "VSMain", vsT, &vsb, error)) return false;
    if (!compile(kSceneShader, "PSMain", psT, &psb, error)) { safeRelease(vsb); return false; }
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsScene_);
    dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &psScene_);
    D3D11_INPUT_ELEMENT_DESC el3d[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"ICOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"IPARAMS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    hr = dev_->CreateInputLayout(el3d, 9, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layoutScene_);
    safeRelease(vsb);
    safeRelease(psb);
    if (FAILED(hr) || !vsScene_ || !psScene_) { error = "Failed to create scene shaders."; return false; }
    if (!compile(kSceneShader, "VSShadow", vsT, &vsb, error)) return false;
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsShadow_);
    safeRelease(vsb);

    // Skinned characters.
    if (!compile(kSceneShader, "VSSkin", vsT, &vsb, error)) return false;
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsSkin_);
    D3D11_INPUT_ELEMENT_DESC elSkin[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"JOINTS", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"WEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    static_assert(sizeof(SkinnedVertex) == 60, "the skinned input layout expects 60-byte vertices");
    hr = dev_->CreateInputLayout(elSkin, 6, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layoutSkin_);
    safeRelease(vsb);
    if (FAILED(hr) || !vsSkin_) { error = "Failed to create skinned vertex shader."; return false; }
    if (!compile(kSceneShader, "VSSkinOutline", vsT, &vsb, error)) return false;
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsSkinOutline_);
    safeRelease(vsb);
    if (!compile(kSceneShader, "VSSkinShadow", vsT, &vsb, error)) return false;
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsSkinShadow_);
    safeRelease(vsb);
    if (!compile(kSceneShader, "VSSkinShadowUV", vsT, &vsb, error)) return false;
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsSkinShadowUV_);
    safeRelease(vsb);
    if (!compile(kSceneShader, "PSCharShadowClip", psT, &psb, error)) return false;
    dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &psCharShadowClip_);
    safeRelease(psb);
    if (!vsSkinShadowUV_ || !psCharShadowClip_) { error = "Failed to create hair shadow shaders."; return false; }
    if (!compile(kSceneShader, "PSChar", psT, &psb, error)) return false;
    dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &psChar_);
    safeRelease(psb);
    if (!compile(kSceneShader, "PSOutline", psT, &psb, error)) return false;
    dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &psOutline_);
    safeRelease(psb);
    if (!vsSkinOutline_ || !vsSkinShadow_ || !psChar_ || !psOutline_) { error = "Failed to create character shaders."; return false; }

    if (!compile(kPostShader, "VSFull", vsT, &vsb, error)) return false;
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsFull_);
    safeRelease(vsb);
    struct { const char* entry; ID3D11PixelShader** out; } posts[] = {
        {"PSLight", &psLight_}, {"PSDown", &psDown_}, {"PSBlur", &psBlur_}, {"PSCompose", &psCompose_},
        {"PSFxaa", &psFxaa_}, {"PSTaa", &psTaa_}, {"PSCopy", &psCopy_}, {"PSDownsample", &psDownsample_},
    };
    for (auto& p : posts) {
        if (!compile(kPostShader, p.entry, psT, &psb, error)) return false;
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, p.out);
        safeRelease(psb);
        if (!*p.out) { error = std::string("Failed to create ") + p.entry; return false; }
    }

    if (!compile(kHudShader, "VSMain", vsT, &vsb, error)) return false;
    if (!compile(kHudShader, "PSMain", psT, &psb, error)) { safeRelease(vsb); return false; }
    dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsHud_);
    dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &psHud_);
    D3D11_INPUT_ELEMENT_DESC elHud[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    hr = dev_->CreateInputLayout(elHud, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layoutHud_);
    safeRelease(vsb);
    safeRelease(psb);
    if (FAILED(hr) || !vsHud_ || !psHud_) { error = "Failed to create HUD shaders."; return false; }
    return true;
}

// ---------------------------------------------------------------- targets
bool RendererD3D11::createTarget(Target& t, int w, int h, DXGI_FORMAT fmt) {
    releaseTarget(t);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = std::max(1, w);
    td.Height = std::max(1, h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &t.tex))) return false;
    dev_->CreateRenderTargetView(t.tex, nullptr, &t.rtv);
    dev_->CreateShaderResourceView(t.tex, nullptr, &t.srv);
    t.w = td.Width;
    t.h = td.Height;
    return t.rtv && t.srv;
}

void RendererD3D11::releaseTarget(Target& t) {
    safeRelease(t.srv);
    safeRelease(t.rtv);
    safeRelease(t.tex);
}

bool RendererD3D11::createTargets(std::string& error) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) {
        error = "Could not get the swap chain back buffer.";
        return false;
    }
    dev_->CreateRenderTargetView(back, nullptr, &backRtv_);
    safeRelease(back);

    renderScale_ = gfx_.get(GfxOption::AntiAliasing) >= 3 ? 2 : 1;
    rw_ = width_ * renderScale_;
    rh_ = height_ * renderScale_;
    bool ok = createTarget(color_, rw_, rh_, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
              createTarget(normal_, rw_, rh_, DXGI_FORMAT_R8G8B8A8_UNORM) &&
              createTarget(lit_, rw_, rh_, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
              createTarget(blurA_, rw_ / 2, rh_ / 2, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
              createTarget(blurB_, rw_ / 2, rh_ / 2, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
              createTarget(ldr_, rw_, rh_, DXGI_FORMAT_R8G8B8A8_UNORM) &&
              createTarget(taa_[0], rw_, rh_, DXGI_FORMAT_R8G8B8A8_UNORM) &&
              createTarget(taa_[1], rw_, rh_, DXGI_FORMAT_R8G8B8A8_UNORM);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = rw_;
    td.Height = rh_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R24G8_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    dev_->CreateTexture2D(&td, nullptr, &depthTex_);
    if (depthTex_) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv = {};
        dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dev_->CreateDepthStencilView(depthTex_, &dv, &dsv_);
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = 1;
        dev_->CreateShaderResourceView(depthTex_, &sv, &depthSrv_);
    }
    historyValid_ = false;
    if (!ok || !backRtv_ || !dsv_ || !depthSrv_) {
        error = "Could not create render targets.";
        return false;
    }
    return true;
}

void RendererD3D11::releaseTargets() {
    if (ctx_) ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    safeRelease(backRtv_);
    for (Target* t : {&color_, &normal_, &lit_, &blurA_, &blurB_, &ldr_, &taa_[0], &taa_[1]}) releaseTarget(*t);
    safeRelease(depthSrv_);
    safeRelease(dsv_);
    safeRelease(depthTex_);
}

bool RendererD3D11::createShadowMap(int size) {
    safeRelease(shadowSrv_);
    safeRelease(shadowDsv_);
    safeRelease(shadowTex_);
    shadowSize_ = 0;
    if (size <= 0) return true;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = size;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &shadowTex_))) return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dev_->CreateDepthStencilView(shadowTex_, &dv, &shadowDsv_);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    dev_->CreateShaderResourceView(shadowTex_, &sv, &shadowSrv_);
    shadowSize_ = (shadowDsv_ && shadowSrv_) ? size : 0;
    return shadowSize_ > 0;
}

void RendererD3D11::resize(int width, int height) {
    if (!swap_ || width <= 0 || height <= 0 || (width == width_ && height == height_)) return;
    releaseTargets();
    width_ = width;
    height_ = height;
    swap_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
    std::string err;
    createTargets(err);
}

void RendererD3D11::setGraphics(const GraphicsSettings& g) {
    int wantScale = g.get(GfxOption::AntiAliasing) >= 3 ? 2 : 1;
    if (wantScale != renderScale_ && swap_) {
        gfx_ = g;
        releaseTargets();
        std::string err;
        createTargets(err);
    }
    gfx_ = g;
    static const int shadowSizes[4] = {0, 1024, 2048, 4096};
    int want = shadowSizes[g.get(GfxOption::Shadows)];
    if (want != shadowSize_ && dev_) createShadowMap(want);
}

// ---------------------------------------------------------------- meshes / textures
bool RendererD3D11::createMesh(const std::vector<MeshVertex>& v, const std::vector<uint32_t>& idx, Mesh& out) {
    releaseMesh(out);
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = (UINT)(v.size() * sizeof(MeshVertex));
    D3D11_SUBRESOURCE_DATA init = {v.data(), 0, 0};
    if (FAILED(dev_->CreateBuffer(&bd, &init, &out.vb))) return false;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = (UINT)(idx.size() * sizeof(uint32_t));
    init.pSysMem = idx.data();
    if (FAILED(dev_->CreateBuffer(&bd, &init, &out.ib))) return false;
    out.indexCount = (UINT)idx.size();
    return true;
}

void RendererD3D11::releaseMesh(Mesh& m) {
    safeRelease(m.vb);
    safeRelease(m.ib);
    m.indexCount = 0;
}

void RendererD3D11::setTerrain(const World& world) {
    static const int steps[4] = {4, 2, 2, 1};
    int step = steps[gfx_.get(GfxOption::MeshDetail)];
    if (world.generation == terrainGeneration_ && step == terrainStep_) return;
    std::vector<MeshVertex> v;
    std::vector<uint32_t> idx;
    makeTerrainMesh(world, v, idx, step);
    createMesh(v, idx, meshes_[MESH_TERRAIN][0]);
    terrainGeneration_ = world.generation;
    terrainStep_ = step;
}

void RendererD3D11::setMapImage(const std::vector<uint32_t>& rgba, int size, int generation) {
    if (generation == mapGeneration_ || (int)rgba.size() < size * size) return;
    safeRelease(mapSrv_);
    safeRelease(mapTex_);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = size;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {rgba.data(), (UINT)size * 4, 0};
    dev_->CreateTexture2D(&td, &init, &mapTex_);
    if (mapTex_) dev_->CreateShaderResourceView(mapTex_, nullptr, &mapSrv_);
    mapGeneration_ = generation;
}

void RendererD3D11::ensureInstanceCapacity(size_t count) {
    if (count <= instanceCap_ && instanceBuf_) return;
    safeRelease(instanceBuf_);
    instanceCap_ = std::max<size_t>(count + count / 2, 4096);
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = (UINT)(instanceCap_ * sizeof(Instance));
    dev_->CreateBuffer(&bd, nullptr, &instanceBuf_);
}

void RendererD3D11::ensureHudCapacity(size_t verts) {
    if (verts <= hudCap_ && hudBuf_) return;
    safeRelease(hudBuf_);
    hudCap_ = std::max<size_t>(verts + verts / 2, 16384);
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = (UINT)(hudCap_ * sizeof(HudVertex));
    dev_->CreateBuffer(&bd, nullptr, &hudBuf_);
}

// Sorts instances into draw ranges, picking a level of detail for round shapes by distance
// (a budget version of what Nanite does automatically).
void RendererD3D11::uploadInstances(const SceneList& scene) {
    static const float lodNear[4] = {-1, -1, 12, 35};   // closer than this: LOD 2
    static const float lodMid[4] = {-1, 20, 45, 120};   // closer than this: LOD 1, else LOD 0
    int detail = gfx_.get(GfxOption::MeshDetail);
    instanceScratch_.clear();
    ranges_.clear();
    // pass 0 opaque, 1 see-through, 2 hidden by occlusion culling (drawn into the shadow map only).
    for (int pass = 0; pass < 3; ++pass)
        for (int m = 0; m < MESH_COUNT; ++m) {
            const std::vector<Instance>& list = pass == 0 ? scene.opaque[m] : (pass == 1 ? scene.transparent[m] : scene.shadowOnly[m]);
            if (list.empty()) continue;
            if (!meshHasLods(m)) {
                ranges_.push_back({m, 0, (UINT)instanceScratch_.size(), (UINT)list.size(), pass == 1, pass == 2});
                instanceScratch_.insert(instanceScratch_.end(), list.begin(), list.end());
                continue;
            }
            for (int lod = 0; lod < MESH_LODS; ++lod) {
                UINT first = (UINT)instanceScratch_.size();
                for (const Instance& in : list) {
                    Vec3 p(in.world.m[3][0], in.world.m[3][1], in.world.m[3][2]);
                    float radius = std::max(std::fabs(in.world.m[0][0]), std::max(std::fabs(in.world.m[1][1]), std::fabs(in.world.m[2][2])));
                    float d = length(p - scene.eye) - radius;
                    // Bigger things keep detail further away.
                    float scale = clampf(radius, 0.5f, 4.0f);
                    int want = d < lodNear[detail] * scale ? 2 : (d < lodMid[detail] * scale ? 1 : 0);
                    if (want == lod) instanceScratch_.push_back(in);
                }
                UINT count = (UINT)instanceScratch_.size() - first;
                if (count) ranges_.push_back({m, lod, first, count, pass == 1, pass == 2});
            }
        }
    ensureInstanceCapacity(instanceScratch_.size());
    if (!instanceBuf_ || instanceScratch_.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ctx_->Map(instanceBuf_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        std::memcpy(ms.pData, instanceScratch_.data(), instanceScratch_.size() * sizeof(Instance));
        ctx_->Unmap(instanceBuf_, 0);
    }
}

void RendererD3D11::drawScene(const SceneList& scene, bool shadowPass) {
    const float blendFactor[4] = {0, 0, 0, 0};
    ctx_->IASetInputLayout(layoutScene_);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (int pass = 0; pass < (shadowPass ? 1 : 2); ++pass) {
        if (!shadowPass) {
            ctx_->OMSetBlendState(pass == 0 ? bsOpaque_ : bsAlpha_, blendFactor, 0xFFFFFFFF);
            ctx_->OMSetDepthStencilState(pass == 0 ? dsOpaque_ : dsTransparent_, 0);
        }
        for (const DrawRange& r : ranges_) {
            if (r.transparent != (pass == 1)) continue;
            if (r.shadowOnly && !shadowPass) continue;
            if (r.mesh == MESH_TERRAIN && !scene.drawTerrain) continue;
            const Mesh& mesh = meshes_[r.mesh][r.lod];
            if (!mesh.vb) continue;
            ID3D11Buffer* bufs[2] = {mesh.vb, instanceBuf_};
            UINT strides[2] = {sizeof(MeshVertex), sizeof(Instance)};
            UINT offs[2] = {0, 0};
            ctx_->IASetVertexBuffers(0, 2, bufs, strides, offs);
            ctx_->IASetIndexBuffer(mesh.ib, DXGI_FORMAT_R32_UINT, 0);
            ctx_->DrawIndexedInstanced(mesh.indexCount, r.count, 0, 0, r.first);
            if (!shadowPass) triangles_ += mesh.indexCount / 3 * r.count;
        }
        // Characters are opaque: draw them before the see-through things.
        if (pass == 0) drawCharacters(scene, shadowPass);
    }
}

// ---------------------------------------------------------------- characters
bool RendererD3D11::loadSkinTexture(const std::string& path, ID3D11ShaderResourceView** out) {
    *out = nullptr;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> bytes((size_t)std::max(0L, n));
    size_t got = n > 0 ? std::fread(bytes.data(), 1, (size_t)n, f) : 0;
    std::fclose(f);
    if (got != bytes.size()) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!px) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 0;  // full chain
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = dev_->CreateTexture2D(&td, nullptr, &tex);
    if (SUCCEEDED(hr) && tex) {
        ctx_->UpdateSubresource(tex, 0, nullptr, px, w * 4, 0);
        hr = dev_->CreateShaderResourceView(tex, nullptr, out);
        if (SUCCEEDED(hr) && *out) ctx_->GenerateMips(*out);
        skinTex_.push_back(tex);
    }
    stbi_image_free(px);
    return *out != nullptr;
}

// Loads the material texture arrays: slice i = MaterialId i (0 = plain, left white).
bool RendererD3D11::loadMaterialTextures(const std::string& dir, std::string& error) {
    static const char* names[MAT_COUNT] = {nullptr, "wood", "stone", "grass", "cloth", "metal", "leaf", "skin", "plaster", "roof", "bark"};
    safeRelease(matAlbedoSrv_);
    safeRelease(matAlbedoTex_);
    safeRelease(matNormalSrv_);
    safeRelease(matNormalTex_);
    int size = 0;
    for (int kind = 0; kind < 2; ++kind) {
        ID3D11Texture2D** tex = kind == 0 ? &matAlbedoTex_ : &matNormalTex_;
        ID3D11ShaderResourceView** srv = kind == 0 ? &matAlbedoSrv_ : &matNormalSrv_;
        std::vector<std::vector<unsigned char>> slices(MAT_COUNT);
        for (int m = 0; m < MAT_COUNT; ++m) {
            if (!names[m]) continue;
            std::string path = dir + "/" + names[m] + (kind == 0 ? "_albedo.png" : "_normal.png");
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) { error = "Missing texture " + path; return false; }
            std::fseek(f, 0, SEEK_END);
            long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            std::vector<unsigned char> bytes((size_t)std::max(0L, n));
            size_t got = n > 0 ? std::fread(bytes.data(), 1, (size_t)n, f) : 0;
            std::fclose(f);
            if (got != bytes.size()) { error = "Could not read " + path; return false; }
            int w = 0, h = 0, comp = 0;
            unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
            if (!px) { error = "Could not decode " + path; return false; }
            if (size == 0) size = w;
            if (w != size || h != size) { stbi_image_free(px); error = "Texture sizes differ: " + path; return false; }
            slices[m].assign(px, px + (size_t)w * h * 4);
            stbi_image_free(px);
        }
        // Slice 0 (plain) and any missing slice: flat (white albedo / flat normal).
        for (int m = 0; m < MAT_COUNT; ++m)
            if (slices[m].empty()) {
                slices[m].assign((size_t)size * size * 4, kind == 0 ? 128 : 128);
                if (kind == 1)
                    for (size_t i = 0; i < slices[m].size(); i += 4) { slices[m][i] = 128; slices[m][i + 1] = 128; slices[m][i + 2] = 255; slices[m][i + 3] = 255; }
            }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = size;
        td.Height = size;
        td.MipLevels = 0;
        td.ArraySize = MAT_COUNT;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, tex)) || !*tex) { error = "Could not create the material texture array."; return false; }
        D3D11_TEXTURE2D_DESC got = {};
        (*tex)->GetDesc(&got);
        for (int m = 0; m < MAT_COUNT; ++m)
            ctx_->UpdateSubresource(*tex, D3D11CalcSubresource(0, m, got.MipLevels), nullptr, slices[m].data(), size * 4, 0);
        if (FAILED(dev_->CreateShaderResourceView(*tex, nullptr, srv)) || !*srv) { error = "Could not view the material textures."; return false; }
        ctx_->GenerateMips(*srv);
    }
    return true;
}

bool RendererD3D11::setCharacterModel(const CharacterModel& model, const std::string& skinDir, std::string& error) {
    for (Mesh& m : charMeshes_) releaseMesh(m);
    charMeshes_.clear();
    for (ID3D11ShaderResourceView*& s : skinSrv_) safeRelease(s);
    safeRelease(charNormalSrv_);
    safeRelease(charMaterialSrv_);
    for (ID3D11Texture2D*& t : skinTex_) safeRelease(t);
    skinSrv_.clear();
    skinTex_.clear();
    charModel_ = nullptr;
    if (!model.loaded) { error = "Character model not loaded."; return false; }
    for (const ModelMesh& mm : model.meshes) {
        Mesh m;
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = (UINT)(mm.verts.size() * sizeof(SkinnedVertex));
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init = {mm.verts.data(), 0, 0};
        if (FAILED(dev_->CreateBuffer(&bd, &init, &m.vb))) { error = "Failed to create a character mesh."; return false; }
        bd.ByteWidth = (UINT)(mm.idx.size() * sizeof(uint32_t));
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        init.pSysMem = mm.idx.data();
        if (FAILED(dev_->CreateBuffer(&bd, &init, &m.ib))) { error = "Failed to create a character mesh."; return false; }
        m.indexCount = (UINT)mm.idx.size();
        charMeshes_.push_back(m);
    }
    // Skin textures: assets/skins/<index>_<name>.png, in index order.
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(skinDir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".png") files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
        auto num = [](const std::string& p) {
            std::string n = std::filesystem::path(p).filename().string();
            return std::atoi(n.c_str());
        };
        return num(a) < num(b);
    });
    for (const std::string& f : files) {
        ID3D11ShaderResourceView* srv = nullptr;
        if (!loadSkinTexture(f, &srv)) { error = "Could not load skin texture " + f; return false; }
        skinSrv_.push_back(srv);
    }
    if (skinSrv_.empty()) { error = "No skin textures in " + skinDir; return false; }
    // The shared detail atlas sits next to the skins folder. Optional: without it characters use
    // their painted textures only (and the outfit pieces aren't drawn).
    std::string atlasDir = std::filesystem::path(skinDir).parent_path().string();
    if (!loadSkinTexture(atlasDir + "/character_normal.png", &charNormalSrv_) ||
        !loadSkinTexture(atlasDir + "/character_material.png", &charMaterialSrv_)) {
        safeRelease(charNormalSrv_);
        safeRelease(charMaterialSrv_);
    }
    charModel_ = &model;
    return true;
}

void RendererD3D11::drawCharacters(const SceneList& scene, bool shadowPass) {
    if (!charModel_ || scene.characters.empty() || !cbBones_ || !cbChar_) return;
    static const float lod1At[4] = {8, 14, 24, 40};
    static const float lod2At[4] = {20, 35, 60, 110};
    int detail = gfx_.get(GfxOption::MeshDetail);
    const bool atlas = hasCharacterAtlas();
    const bool realistic = charStyle_ == 1 && atlas;
    ctx_->IASetInputLayout(layoutSkin_);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbs[3] = {cbFrame_, cbBones_, cbChar_};
    ctx_->VSSetConstantBuffers(0, 3, cbs);
    ctx_->PSSetConstantBuffers(0, 3, cbs);
    ctx_->PSSetSamplers(2, 1, &sampSkin_);
    if (atlas) {
        ID3D11ShaderResourceView* maps[2] = {charNormalSrv_, charMaterialSrv_};
        ctx_->PSSetShaderResources(4, 2, maps);
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    auto drawMesh = [&](int id) {
        if (id < 0 || id >= (int)charMeshes_.size()) return;
        const Mesh& mesh = charMeshes_[id];
        UINT stride = sizeof(SkinnedVertex), off = 0;
        ctx_->IASetVertexBuffers(0, 1, &mesh.vb, &stride, &off);
        ctx_->IASetIndexBuffer(mesh.ib, DXGI_FORMAT_R32_UINT, 0);
        ctx_->DrawIndexed(mesh.indexCount, 0, 0);
        if (!shadowPass) triangles_ += mesh.indexCount / 3;
    };
    CharCB cb;
    auto upload = [&](int kind) {
        cb.flags.x = (float)kind;
        if (SUCCEEDED(ctx_->Map(cbChar_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &cb, sizeof cb);
            ctx_->Unmap(cbChar_, 0);
        }
    };
    for (const CharacterDraw& c : scene.characters) {
        if (c.boneCount <= 0 || c.boneOffset + c.boneCount > (int)scene.boneMats.size()) continue;
        if (c.shadowOnly && !shadowPass) continue;   // hidden behind something (occlusion culling)
        if (SUCCEEDED(ctx_->Map(cbBones_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &scene.boneMats[c.boneOffset], sizeof(Mat4) * std::min(c.boneCount, MAX_BONES));
            ctx_->Unmap(cbBones_, 0);
        }
        int lod = c.distance < lod1At[detail] ? 0 : (c.distance < lod2At[detail] ? 1 : 2);
        float outline = !realistic && c.distance < lod2At[detail] ? 0.009f : 0.0f;
        const bool close = lod < 2;
        const bool faceParts = atlas && close && c.face;

        // Palette: what each atlas zone looks like on this skin.
        cb.world = c.world;
        cb.tint = c.tint;
        cb.accent = c.accent;
        cb.params = Vec4(c.params.x, c.params.y, c.params.z, outline);
        cb.flags = Vec4(0, faceParts ? 1.0f : 0.0f, realistic ? 1.0f : 0.0f, atlas ? 1.0f : 0.0f);
        for (int z = 0; z < 16; ++z) {
            cb.zoneColor[z] = Vec4(1, 1, 1, 1);
            cb.zoneMat[z] = Vec4(-1, -1, 0, 0);
        }
        float kr = 0.85f, km = 0.0f;
        kitMaterialParams(c.kitMaterial, kr, km);
        cb.zoneColor[ZONE_MAIN] = c.main;
        cb.zoneColor[ZONE_SECOND] = c.second;
        cb.zoneColor[ZONE_ACCENT] = c.trim;
        cb.zoneColor[ZONE_LEATHER] = Vec4(0.24f, 0.16f, 0.1f, 1);
        cb.zoneColor[ZONE_METAL] = Vec4(0.8f, 0.8f, 0.83f, 1);
        cb.zoneColor[ZONE_HAIR] = c.hairColor;
        cb.zoneColor[ZONE_SKIN] = c.skinTone;
        cb.zoneColor[ZONE_EYE] = Vec4(0.93f, 0.93f, 0.92f, 1);
        cb.zoneColor[ZONE_DARK] = Vec4(0.04f, 0.04f, 0.05f, 1);
        cb.zoneColor[ZONE_LIPS] = Vec4(c.skinTone.x * 0.82f, c.skinTone.y * 0.52f, c.skinTone.z * 0.5f, 1);
        cb.zoneColor[ZONE_LENS] = Vec4(0.06f, 0.16f, 0.22f, 1);
        cb.zoneMat[ZONE_MAIN] = Vec4(kr, km, 0, 0);
        cb.zoneMat[ZONE_ACCENT] = Vec4(kr, km, 0, 0);
        // Emission FX: a slow pulse so energy cracks and circuits look alive.
        float pulse = 0.8f + 0.2f * std::sin(frame_ / 60.0f * 3.0f + (float)c.skin);
        cb.fx = Vec4((c.fx & SKIN_FX_TRIM) ? 0.9f * pulse : 0.0f, (c.fx & SKIN_FX_EYES) ? 1.2f : 0.0f, 0, 0);

        int skin = skinSrv_.empty() ? 0 : ((c.skin % (int)skinSrv_.size()) + (int)skinSrv_.size()) % (int)skinSrv_.size();
        ID3D11ShaderResourceView* srv = skinSrv_[skin];
        if (!shadowPass) ctx_->PSSetShaderResources(1, 1, &srv);

        bool hairShows = c.hair >= 0 && c.hair < 4 && (c.hat <= 0 || c.hair == 1 || c.hair == 3);  // spikes/mohawk hide under hats
        bool wearsMask = (c.kit >> (int)KitPiece::Mask) & 1u;
        const int hairKind = atlas ? KIND_ATLAS : KIND_PAINTED;   // old models: solid hair painted from the skin
        // Solid parts first, then the hair cards (alpha-tested in the shadow map).
        auto pass = [&](bool outlinePass) {
            upload(KIND_BODY);
            drawMesh(charModel_->bodyMesh(c.build, lod));
            if (!close) return;
            upload(KIND_PAINTED);
            if (c.hat > 0 && c.hat < 4) drawMesh(charModel_->hat[c.hat]);
            if (c.headband && !outlinePass) drawMesh(charModel_->headband);
            if (atlas) {
                upload(KIND_ATLAS);
                for (int k = 0; k < (int)KitPiece::Count; ++k)
                    if ((c.kit >> k) & 1u) drawMesh(charModel_->kitMesh(k, c.build));
                if (faceParts && !outlinePass && !shadowPass) {
                    drawMesh(charModel_->face);
                    if (!wearsMask) drawMesh(charModel_->mouth);
                }
            }
            if (hairShows && !outlinePass) {
                upload(hairKind);
                if (shadowPass && atlas) {
                    ctx_->VSSetShader(vsSkinShadowUV_, nullptr, 0);
                    ctx_->PSSetShader(psCharShadowClip_, nullptr, 0);
                    drawMesh(charModel_->hair[c.hair]);
                    ctx_->VSSetShader(vsSkinShadow_, nullptr, 0);
                    ctx_->PSSetShader(nullptr, nullptr, 0);
                } else {
                    drawMesh(charModel_->hair[c.hair]);
                }
            }
        };
        ctx_->VSSetShader(shadowPass ? vsSkinShadow_ : vsSkin_, nullptr, 0);
        ctx_->PSSetShader(shadowPass ? nullptr : psChar_, nullptr, 0);
        ctx_->RSSetState(shadowPass ? rsShadow_ : rsSolid_);
        pass(false);
        if (!shadowPass && outline > 0.0f && gfx_.get(GfxOption::SmoothShading) > 0) {
            ctx_->VSSetShader(vsSkinOutline_, nullptr, 0);
            ctx_->PSSetShader(psOutline_, nullptr, 0);
            ctx_->RSSetState(rsCullFront_);
            pass(true);
            ctx_->RSSetState(rsSolid_);
        }
    }
    if (!shadowPass) {
        ID3D11ShaderResourceView* none[1] = {nullptr};
        ctx_->PSSetShaderResources(1, 1, none);
    }
    if (atlas) {
        ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(4, 2, none);
    }
    // Back to the instanced scene state.
    ctx_->IASetInputLayout(layoutScene_);
    ctx_->VSSetShader(shadowPass ? vsShadow_ : vsScene_, nullptr, 0);
    ctx_->PSSetShader(shadowPass ? nullptr : psScene_, nullptr, 0);
}

void RendererD3D11::fullscreen(ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, int w, int h,
                               ID3D11ShaderResourceView* const* srvs, int n) {
    ID3D11ShaderResourceView* none[4] = {nullptr, nullptr, nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 4, none);
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vsFull_, nullptr, 0);
    ctx_->PSSetShader(ps, nullptr, 0);
    ctx_->PSSetShaderResources(0, n, srvs);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 4, none);
}

// ---------------------------------------------------------------- frame
void RendererD3D11::render(const SceneList& scene, const std::vector<HudQuad>& hud, float dofFocus) {
    if (!ctx_ || !backRtv_) return;
    ++frame_;
    triangles_ = 0;
    const int aa = gfx_.get(GfxOption::AntiAliasing);
    const bool taa = aa >= 2;
    const float blendFactor[4] = {0, 0, 0, 0};
    D3D11_MAPPED_SUBRESOURCE ms;

    // Jitter the projection a little each frame for temporal AA (undone when resolving).
    float jx = 0, jy = 0;
    if (taa) {
        unsigned k = frame_ % 8 + 1;
        jx = (halton(k, 2) - 0.5f) * 2.0f / rw_;
        jy = (halton(k, 3) - 0.5f) * 2.0f / rh_;
    }
    Mat4 proj = scene.proj;
    proj.m[2][0] += jx;
    proj.m[2][1] += jy;
    Mat4 viewProjJ = scene.view * proj;
    Mat4 viewProj = scene.view * scene.proj;

    // Sun shadow camera: an orthographic box around the area ahead of the camera, snapped to
    // shadow-map texels so the edges don't crawl when the camera moves.
    const float shadowSpan = 150.0f;
    Mat4 lightViewProj = Mat4::identity();
    float texelWorld = shadowSpan / std::max(1, shadowSize_);
    if (shadowSize_ > 0 && scene.drawTerrain) {
        Vec3 f = scene.eye;  // (camera-centred; the scene's forward isn't needed)
        Vec3 up = std::fabs(scene.sunDir.y) > 0.95f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        Mat4 lv = Mat4::lookAtLH(f - scene.sunDir * 220.0f, f, up);
        Vec3 c = transformPoint(f, lv);
        float snapX = std::floor(c.x / texelWorld) * texelWorld - c.x;
        float snapY = std::floor(c.y / texelWorld) * texelWorld - c.y;
        lv = lv * Mat4::translation(Vec3(snapX, snapY, 0));
        lightViewProj = lv * Mat4::orthoLH(shadowSpan, shadowSpan, 1.0f, 450.0f);
    } else if (shadowSize_ > 0) {
        Vec3 c(0, 0, 1);
        Mat4 lv = Mat4::lookAtLH(c - scene.sunDir * 60.0f, c, Vec3(0, 1, 0));
        lightViewProj = lv * Mat4::orthoLH(40.0f, 40.0f, 1.0f, 150.0f);
        texelWorld = 40.0f / shadowSize_;
    }

    uploadInstances(scene);

    // 1) Shadow map.
    if (shadowSize_ > 0) {
        if (SUCCEEDED(ctx_->Map(cbFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            FrameCB* f = (FrameCB*)ms.pData;
            *f = FrameCB();
            f->viewProj = viewProjJ;
            f->lightViewProj = lightViewProj;
            ctx_->Unmap(cbFrame_, 0);
        }
        ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(0, 2, none);
        ctx_->ClearDepthStencilView(shadowDsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
        ctx_->OMSetRenderTargets(0, nullptr, shadowDsv_);
        D3D11_VIEWPORT vp = {0, 0, (float)shadowSize_, (float)shadowSize_, 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ctx_->RSSetState(rsShadow_);
        ctx_->OMSetBlendState(bsOpaque_, blendFactor, 0xFFFFFFFF);
        ctx_->OMSetDepthStencilState(dsOpaque_, 0);
        ctx_->VSSetShader(vsShadow_, nullptr, 0);
        ctx_->PSSetShader(nullptr, nullptr, 0);
        ctx_->VSSetConstantBuffers(0, 1, &cbFrame_);
        drawScene(scene, true);
    }

    // 2) Scene into colour + normal + depth.
    if (SUCCEEDED(ctx_->Map(cbFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        FrameCB* f = (FrameCB*)ms.pData;
        f->viewProj = viewProjJ;
        f->lightViewProj = lightViewProj;
        f->sunDir = Vec4(scene.sunDir.x, scene.sunDir.y, scene.sunDir.z, 0);
        f->eyePos = Vec4(scene.eye.x, scene.eye.y, scene.eye.z, 1);
        f->sky = scene.sky;
        f->gfx = Vec4((float)gfx_.get(GfxOption::SmoothShading), (float)gfx_.get(GfxOption::NormalMaps),
                      (float)gfx_.get(GfxOption::Materials), (float)gfx_.get(GfxOption::Lighting));
        f->gfx2 = Vec4(shadowSize_ > 0 ? (float)gfx_.get(GfxOption::Shadows) : 0.0f, (float)std::max(1, shadowSize_), texelWorld,
                       frame_ / 60.0f);
        f->gfx3 = Vec4(matAlbedoSrv_ ? 1.0f : 0.0f, 0, 0, 0);
        ctx_->Unmap(cbFrame_, 0);
    }
    const float clear[4] = {scene.sky.x, scene.sky.y, scene.sky.z, 1};
    const float clearN[4] = {0.5f, 1.0f, 0.5f, 1.0f};
    ctx_->ClearRenderTargetView(color_.rtv, clear);
    ctx_->ClearRenderTargetView(normal_.rtv, clearN);
    ctx_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ID3D11RenderTargetView* rts[2] = {color_.rtv, normal_.rtv};
    ctx_->OMSetRenderTargets(2, rts, dsv_);
    D3D11_VIEWPORT vp = {0, 0, (float)rw_, (float)rh_, 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(rsSolid_);
    ctx_->VSSetShader(vsScene_, nullptr, 0);
    ctx_->PSSetShader(psScene_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cbFrame_);
    ctx_->PSSetConstantBuffers(0, 1, &cbFrame_);
    ID3D11ShaderResourceView* sceneSrv[4] = {shadowSrv_, nullptr, matAlbedoSrv_, matNormalSrv_};
    ctx_->PSSetShaderResources(0, 4, sceneSrv);
    ID3D11SamplerState* sceneSamps[4] = {sampShadow_, sampPoint_, sampSkin_, sampWrap_};
    ctx_->PSSetSamplers(0, 4, sceneSamps);
    drawScene(scene, false);
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);

    // 3..6) Full-screen passes.
    Mat4 invVP = inverse(viewProj);
    auto postCB = [&](float blurDir) {
        if (SUCCEEDED(ctx_->Map(cbPost_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            PostCB* p = (PostCB*)ms.pData;
            p->invViewProj = invVP;
            p->prevViewProj = historyValid_ ? prevViewProj_ : viewProj;
            p->p0 = Vec4((float)rw_, (float)rh_, 0.1f, 600.0f);
            p->p1 = Vec4((float)gfx_.get(GfxOption::Lighting), (float)gfx_.get(GfxOption::PostFx), (float)aa, frame_ / 60.0f);
            p->p2 = Vec4(scene.drawTerrain ? 140.0f : 60.0f, scene.drawTerrain ? 480.0f : 200.0f, dofFocus, blurDir);
            p->sky = scene.sky;
            p->eye = Vec4(scene.eye.x, scene.eye.y, scene.eye.z, 1);
            p->p3 = Vec4(historyValid_ ? 0.1f : 1.0f, aa >= 3 ? 0.35f : 0.0f, jx * 0.5f, -jy * 0.5f);
            ctx_->Unmap(cbPost_, 0);
        }
    };
    ID3D11SamplerState* postSamps[2] = {sampLinear_, sampPoint_};
    ctx_->PSSetSamplers(0, 2, postSamps);
    ctx_->PSSetConstantBuffers(0, 1, &cbPost_);
    ctx_->OMSetBlendState(bsOpaque_, blendFactor, 0xFFFFFFFF);
    ctx_->OMSetDepthStencilState(dsOff_, 0);

    postCB(0);
    {   // lighting: AO + bounce
        ID3D11ShaderResourceView* in[3] = {color_.srv, normal_.srv, depthSrv_};
        fullscreen(psLight_, lit_.rtv, rw_, rh_, in, 3);
    }
    const int post = gfx_.get(GfxOption::PostFx);
    if (post >= 2) {
        // half-res blur for bloom / depth of field
        ID3D11ShaderResourceView* in1[1] = {lit_.srv};
        fullscreen(psDown_, blurA_.rtv, blurA_.w, blurA_.h, in1, 1);
        // The blur shader wants the half-res size in p0.
        if (SUCCEEDED(ctx_->Map(cbPost_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            PostCB* p = (PostCB*)ms.pData;
            *p = PostCB();
            p->p0 = Vec4((float)blurA_.w, (float)blurA_.h, 0.1f, 600.0f);
            p->p2 = Vec4(0, 0, 0, 0);
            ctx_->Unmap(cbPost_, 0);
        }
        ID3D11ShaderResourceView* inA[1] = {blurA_.srv};
        fullscreen(psBlur_, blurB_.rtv, blurB_.w, blurB_.h, inA, 1);
        if (SUCCEEDED(ctx_->Map(cbPost_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            PostCB* p = (PostCB*)ms.pData;
            *p = PostCB();
            p->p0 = Vec4((float)blurA_.w, (float)blurA_.h, 0.1f, 600.0f);
            p->p2 = Vec4(0, 0, 0, 1);
            ctx_->Unmap(cbPost_, 0);
        }
        ID3D11ShaderResourceView* inB[1] = {blurB_.srv};
        fullscreen(psBlur_, blurA_.rtv, blurA_.w, blurA_.h, inB, 1);
        postCB(0);
    }
    {   // compose
        ID3D11ShaderResourceView* in[3] = {lit_.srv, blurA_.srv, depthSrv_};
        fullscreen(psCompose_, ldr_.rtv, rw_, rh_, in, 3);
    }
    Target& out = taa_[frame_ & 1];
    Target& hist = taa_[(frame_ + 1) & 1];
    if (aa == 1) {
        ID3D11ShaderResourceView* in[1] = {ldr_.srv};
        fullscreen(psFxaa_, out.rtv, rw_, rh_, in, 1);
    } else if (taa) {
        ID3D11ShaderResourceView* in[3] = {ldr_.srv, hist.srv, depthSrv_};
        fullscreen(psTaa_, out.rtv, rw_, rh_, in, 3);
        historyValid_ = true;
    } else {
        ID3D11ShaderResourceView* in[1] = {ldr_.srv};
        fullscreen(psCopy_, out.rtv, rw_, rh_, in, 1);
    }
    if (!taa) historyValid_ = false;
    prevViewProj_ = viewProj;

    // To the back buffer.
    if (renderScale_ == 1) {
        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) {
            ctx_->CopyResource(back, out.tex);
            safeRelease(back);
        }
    } else {
        // Supersampled: average the 2x render down to the window.
        ID3D11ShaderResourceView* in[1] = {out.srv};
        fullscreen(psDownsample_, backRtv_, width_, height_, in, 1);
    }

    // 7) HUD.
    if (!hud.empty()) {
        size_t verts = hud.size() * 6;
        ensureHudCapacity(verts);
        if (hudBuf_ && SUCCEEDED(ctx_->Map(hudBuf_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            HudVertex* v = (HudVertex*)ms.pData;
            for (const HudQuad& q : hud) {
                HudVertex a{q.x, q.y, q.u0, q.v0, q.color}, b{q.x + q.w, q.y, q.u1, q.v0, q.color};
                HudVertex c{q.x + q.w, q.y + q.h, q.u1, q.v1, q.color}, d{q.x, q.y + q.h, q.u0, q.v1, q.color};
                *v++ = a; *v++ = b; *v++ = c;
                *v++ = a; *v++ = c; *v++ = d;
            }
            ctx_->Unmap(hudBuf_, 0);
        }
        if (SUCCEEDED(ctx_->Map(cbHud_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            float* s = (float*)ms.pData;
            s[0] = (float)width_; s[1] = (float)height_; s[2] = 0; s[3] = 0;
            ctx_->Unmap(cbHud_, 0);
        }
        ctx_->OMSetRenderTargets(1, &backRtv_, nullptr);
        D3D11_VIEWPORT hvp = {0, 0, (float)width_, (float)height_, 0, 1};
        ctx_->RSSetViewports(1, &hvp);
        UINT stride = sizeof(HudVertex), off = 0;
        ctx_->IASetVertexBuffers(0, 1, &hudBuf_, &stride, &off);
        ctx_->IASetInputLayout(layoutHud_);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(vsHud_, nullptr, 0);
        ctx_->PSSetShader(psHud_, nullptr, 0);
        ctx_->VSSetConstantBuffers(0, 1, &cbHud_);
        ctx_->PSSetSamplers(0, 1, &sampLinear_);
        ctx_->OMSetBlendState(bsAlpha_, blendFactor, 0xFFFFFFFF);
        ctx_->OMSetDepthStencilState(dsOff_, 0);
        // Draw runs of quads that share a texture.
        size_t start = 0;
        while (start < hud.size()) {
            int tex = hud[start].tex;
            size_t end = start;
            while (end < hud.size() && hud[end].tex == tex) ++end;
            ID3D11ShaderResourceView* srv = (tex == HUD_TEX_MAP && mapSrv_) ? mapSrv_ : atlasSrv_;
            ctx_->PSSetShaderResources(0, 1, &srv);
            ctx_->Draw((UINT)((end - start) * 6), (UINT)(start * 6));
            start = end;
        }
        ID3D11ShaderResourceView* none[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none);
    }
}

void RendererD3D11::present(bool vsync) {
    if (swap_) swap_->Present(vsync ? 1 : 0, 0);
}

bool RendererD3D11::saveScreenshot(const char* path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) return false;
    D3D11_TEXTURE2D_DESC td;
    back->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &staging))) {
        ctx_->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx_->Map(staging, 0, D3D11_MAP_READ, 0, &ms))) {
            FILE* f = std::fopen(path, "wb");
            if (f) {
                int w = (int)td.Width, h = (int)td.Height, rowBytes = (w * 3 + 3) & ~3;
                unsigned char hdr[54] = {'B', 'M'};
                auto put32 = [&](int at, unsigned v) { for (int i = 0; i < 4; ++i) hdr[at + i] = (unsigned char)(v >> (8 * i)); };
                put32(2, 54 + rowBytes * h);
                put32(10, 54);
                put32(14, 40);
                put32(18, (unsigned)w);
                put32(22, (unsigned)h);
                hdr[26] = 1;
                hdr[28] = 24;
                put32(34, (unsigned)(rowBytes * h));
                std::fwrite(hdr, 1, 54, f);
                std::vector<unsigned char> row(rowBytes, 0);
                for (int y = h - 1; y >= 0; --y) {
                    const unsigned char* src = (const unsigned char*)ms.pData + (size_t)y * ms.RowPitch;
                    for (int x = 0; x < w; ++x) {
                        row[x * 3 + 0] = src[x * 4 + 2];
                        row[x * 3 + 1] = src[x * 4 + 1];
                        row[x * 3 + 2] = src[x * 4 + 0];
                    }
                    std::fwrite(row.data(), 1, rowBytes, f);
                }
                std::fclose(f);
                ok = true;
            }
            ctx_->Unmap(staging, 0);
        }
        safeRelease(staging);
    }
    safeRelease(back);
    return ok;
}

void RendererD3D11::shutdown() {
    for (Mesh& m : charMeshes_) releaseMesh(m);
    charMeshes_.clear();
    for (ID3D11ShaderResourceView*& v : skinSrv_) safeRelease(v);
    safeRelease(charNormalSrv_);
    safeRelease(charMaterialSrv_);
    for (ID3D11Texture2D*& t : skinTex_) safeRelease(t);
    skinSrv_.clear();
    skinTex_.clear();
    safeRelease(vsSkin_);
    safeRelease(vsSkinShadow_);
    safeRelease(vsSkinShadowUV_);
    safeRelease(psCharShadowClip_);
    safeRelease(vsSkinOutline_);
    safeRelease(psChar_);
    safeRelease(psOutline_);
    safeRelease(layoutSkin_);
    safeRelease(cbBones_);
    safeRelease(cbChar_);
    safeRelease(rsCullFront_);
    safeRelease(sampSkin_);
    safeRelease(sampWrap_);
    safeRelease(matAlbedoSrv_);
    safeRelease(matAlbedoTex_);
    safeRelease(matNormalSrv_);
    safeRelease(matNormalTex_);
    if (ctx_) ctx_->ClearState();
    releaseTargets();
    createShadowMap(0);
    for (auto& row : meshes_)
        for (Mesh& m : row) releaseMesh(m);
    safeRelease(mapSrv_);
    safeRelease(mapTex_);
    safeRelease(atlasSrv_);
    safeRelease(atlasTex_);
    safeRelease(sampShadow_);
    safeRelease(sampPoint_);
    safeRelease(sampLinear_);
    safeRelease(bsAlpha_);
    safeRelease(bsOpaque_);
    safeRelease(dsOff_);
    safeRelease(dsTransparent_);
    safeRelease(dsOpaque_);
    safeRelease(rsShadow_);
    safeRelease(rsSolid_);
    safeRelease(hudBuf_);
    safeRelease(instanceBuf_);
    safeRelease(cbHud_);
    safeRelease(cbPost_);
    safeRelease(cbFrame_);
    safeRelease(layoutHud_);
    safeRelease(psHud_);
    safeRelease(vsHud_);
    for (ID3D11PixelShader** p : {&psLight_, &psDown_, &psBlur_, &psCompose_, &psFxaa_, &psTaa_, &psCopy_}) safeRelease(*p);
    safeRelease(vsFull_);
    safeRelease(layoutScene_);
    safeRelease(vsShadow_);
    safeRelease(psScene_);
    safeRelease(vsScene_);
    safeRelease(swap_);
    safeRelease(ctx_);
    safeRelease(dev_);
}

}  // namespace bf
