// The void behind the worlds: a nebula baked into a cube map, plus stars computed live per pixel.

#include "common.hlsli"

TextureCube gNebula : register(t1);
SamplerState gLinear : register(s1);

struct VSOut
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

// Full-screen triangle. dir is the world-space view ray through each pixel.
VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 nearPoint = mul(float4(ndc, 0.0, 1.0), gInvViewProj);
    float4 farPoint = mul(float4(ndc, 1.0, 1.0), gInvViewProj);
    VSOut o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.dir = farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w;
    return o;
}

float3 NebulaColor(float3 d)
{
    float clouds = Fbm(d * 1.7);
    float detail = Fbm(d * 3.4 + float3(5.2, 1.3, 7.7));
    float shape = smoothstep(0.38, 0.78, clouds);
    // Venom: crimson clouds fading into violet, like the glow behind the Venom Board avatar.
    float3 fang = float3(0.34, 0.03, 0.10);
    float3 violet = float3(0.12, 0.05, 0.30);
    float3 color = lerp(fang, violet, smoothstep(0.35, 0.7, detail)) * shape * shape * 0.6;
    color += float3(0.05, 0.015, 0.045) * smoothstep(0.25, 0.75, detail);  // faint haze everywhere
    // A brighter band, as if looking along a galaxy's plane.
    float bandOffset = d.y * 2.2 + (clouds - 0.5) * 0.8;
    float band = exp(-bandOffset * bandOffset * 3.0);
    color += float3(0.13, 0.04, 0.11) * band * smoothstep(0.3, 0.8, detail);
    return color;
}

// Run once per cube face at startup (Renderer::CreateNebula).
float4 NebulaPS(VSOut i) : SV_Target
{
    return float4(NebulaColor(normalize(i.dir)), 1.0);
}

// One layer of stars: a random point in some 3D cells, lit where the view sphere passes close to it.
float3 StarLayer(float3 d, float scale, float threshold, float pixelAngle)
{
    float3 p = d * scale;
    float3 cell = floor(p);
    float h = Hash13(cell);
    float3 jitter = float3(Hash13(cell + 11.1), Hash13(cell + 23.7), Hash13(cell + 37.3)) * 0.7 + 0.15;
    float dist = length(p - cell - jitter);
    float radius = max(0.04, pixelAngle * scale * 1.1);  // never smaller than about a pixel
    float core = saturate(1.0 - dist / radius);
    core *= core;
    float brightness = saturate((h - threshold) / (1.0 - threshold));
    float twinkle = 0.75 + 0.25 * sin(gTime * (0.7 + h * 2.3) + h * 91.0);
    float3 tint = lerp(float3(0.62, 0.76, 1.0), float3(1.0, 0.86, 0.72), Hash13(cell + 5.5));
    return h > threshold ? tint * core * (0.35 + brightness * 1.4) * twinkle : float3(0.0, 0.0, 0.0);
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 d = normalize(i.dir);
    float pixelAngle = length(fwidth(d));
    float3 color = float3(0.004, 0.006, 0.014);
    color += gNebula.SampleLevel(gLinear, d, 0).rgb;
    float3 stars = StarLayer(d, 160.0, 0.93, pixelAngle) + StarLayer(d, 380.0, 0.965, pixelAngle) * 0.7;
    color += stars * (0.35 + 0.65 * gVoidAmount);

    float2 uv = i.pos.xy / gScreen;
    float vignette = smoothstep(1.15, 0.35, length((uv - 0.5) * float2(1.2, 1.0)));
    color *= lerp(0.55, 1.0, vignette);
    color += (Hash13(float3(i.pos.xy, frac(gTime) * 61.0)) - 0.5) / 255.0;  // dither away banding
    return float4(color, 1.0);
}
