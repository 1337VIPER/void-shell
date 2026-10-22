// Shaded spheres: the planet body under the desktop (gKind 0) and the plus world (gKind 1).

#include "common.hlsli"

struct VSIn
{
    float3 pos : POSITION;
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float3 world : TEXCOORD0;
};

VSOut VSMain(VSIn input)
{
    VSOut o;
    float3 world = gCenter + input.pos * gRadius;
    o.pos = mul(float4(world, 1.0), gViewProj);
    o.normal = input.pos;
    o.world = world;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 n = normalize(i.normal);
    float3 v = normalize(gCamPos - i.world);
    float3 l = LightDir();
    float diffuse = saturate(dot(n, l));
    float rim = pow(1.0 - saturate(dot(n, v)), 2.5);
    float spec = pow(saturate(dot(reflect(-l, n), v)), 60.0);

    // Where this point sits across the ball as the camera sees it, for drawing the plus.
    float a = dot(n, gCamRight);
    float b = dot(n, gCamUp);
    float aa = fwidth(a) + fwidth(b);

    float3 color;
    if (gKind < 0.5)
    {
        // Planet body: dark banded rock with an atmospheric rim.
        float bands = Noise3(n * 5.0) * 0.6 + Noise3(n * 15.0) * 0.4;
        float3 rock = lerp(float3(0.018, 0.016, 0.026), float3(0.065, 0.035, 0.070), bands);
        color = rock * (0.35 + 0.9 * diffuse) + gTint.rgb * rim * (1.0 + gHover) + spec * 0.08;
    }
    else
    {
        // Plus world: black glass with a bone-white plus glowing in the rim colour on the side facing the camera.
        float3 glass = lerp(float3(0.020, 0.018, 0.028), float3(0.070, 0.040, 0.080), n.y * 0.5 + 0.5);
        color = glass * (0.45 + 0.8 * diffuse) + gTint.rgb * rim * (1.0 + 1.2 * gHover + gPulse);
        float arm = 0.085;
        float len = 0.40;
        float plusDist = min(max(abs(a) - arm, abs(b) - len), max(abs(b) - arm, abs(a) - len));
        float plus = 1.0 - smoothstep(-aa, aa, plusDist);
        float plusGlow = exp(-max(plusDist, 0.0) * 16.0) * 0.45;
        float energy = 0.85 + 0.5 * gHover + 1.2 * gPulse;
        color += (float3(0.945, 0.933, 0.949) * plus + gTint.rgb * plusGlow) * energy;
        color += spec * 0.5;
    }
    return float4(color, 1.0);
}
