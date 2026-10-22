// Screen-space text. gRect is x, y, width, height in pixels; the texture's red channel is glyph coverage.

#include "common.hlsli"

Texture2D gText : register(t0);
SamplerState gLinear : register(s1);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 corner = float2(id & 1, id >> 1);
    float2 pixel = gRect.xy + corner * gRect.zw;
    VSOut o;
    o.pos = float4(pixel.x / gScreen.x * 2.0 - 1.0, 1.0 - pixel.y / gScreen.y * 2.0, 0.0, 1.0);
    o.uv = corner;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float coverage = gText.Sample(gLinear, i.uv).r;
    return float4(gTint.rgb, coverage * gAlpha);
}

// The remove badge: a disc in gTint with a bone-white cross and a soft glow, drawn in the same screen quad.
float4 BadgePS(VSOut i) : SV_Target
{
    float2 p = i.uv * 2.0 - 1.0;
    float r = length(p);
    float pixel = 2.0 / max(gRect.z, 1.0);
    float disc = saturate((0.62 - r) / pixel + 0.5);
    float glow = exp(-max(r - 0.62, 0.0) * 7.0) * 0.55 * (1.0 - disc);
    float2 q = float2(p.x + p.y, p.x - p.y) * 0.70710678;
    float bar = min(max(abs(q.x) - 0.055, abs(q.y) - 0.28), max(abs(q.y) - 0.055, abs(q.x) - 0.28));
    float cross = saturate(-bar / pixel + 0.5) * disc;
    float3 color = lerp(gTint.rgb, float3(0.945, 0.933, 0.949), cross);
    return float4(color, max(disc, glow) * gAlpha);
}
