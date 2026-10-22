// A camera-facing halo around a ball, added on top of the scene. The ball's own depth hides the middle.

#include "common.hlsli"

static const float kExtent = 2.6;  // quad half-size, in ball radii

struct VSOut
{
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 corner = float2(id & 1, id >> 1) * 2.0 - 1.0;
    float3 world = gCenter + (gCamRight * corner.x + gCamUp * corner.y) * gRadius * kExtent;
    VSOut o;
    o.pos = mul(float4(world, 1.0), gViewProj);
    o.local = corner * kExtent;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float r = length(i.local);
    float halo = exp(-max(r - 1.0, 0.0) * 3.2) * (1.0 - smoothstep(kExtent * 0.7, kExtent, r));
    return float4(gTint.rgb * halo * gAlpha, 0.0);
}
