// The desktop snapshot. At morph 0 it is a flat quad that exactly fills the starting view, so the first
// frame matches the real desktop pixel for pixel. At morph 1 the same pixels wrap onto the front of a ball.

#include "common.hlsli"

Texture2D gDesktop : register(t0);
Texture2D gDesktopPrevious : register(t2);  // the picture being faded out after a refresh (gAlpha = fade-in)
SamplerState gDesktopSampler : register(s0);

struct VSIn
{
    float2 uv : TEXCOORD0;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 world : TEXCOORD1;
};

VSOut VSMain(VSIn input)
{
    float2 uv = input.uv;
    float3 flat = float3((uv.x - 0.5) * 2.0 * gAspect, (0.5 - uv.y) * 2.0, 0.0);

    // The ball is a unit sphere one unit behind the screen; the desktop covers its camera-facing side.
    float lon = (uv.x - 0.5) * 2.0 * gLonSpan;
    float lat = (0.5 - uv.y) * 2.0 * gLatSpan;
    float3 dir = float3(cos(lat) * sin(lon), sin(lat), -cos(lat) * cos(lon));
    float3 ball = float3(0.0, 0.0, 1.0) + dir;

    float3 p = lerp(flat, ball, gMorph) * gRadius + gCenter;
    VSOut o;
    o.pos = mul(float4(p, 1.0), gViewProj);
    o.uv = uv;
    o.normal = lerp(float3(0.0, 0.0, -1.0), dir, gMorph);
    o.world = p;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 color = gDesktop.Sample(gDesktopSampler, i.uv).rgb;
    float3 previous = gDesktopPrevious.Sample(gDesktopSampler, i.uv).rgb;
    color = gAlpha >= 1.0 ? color : lerp(previous, color, gAlpha);
    float m = gMorph;

    float3 n = normalize(i.normal);
    float3 v = normalize(gCamPos - i.world);
    float3 l = LightDir();
    float diffuse = 0.62 + 0.55 * saturate(dot(n, l));
    float rim = pow(1.0 - saturate(dot(n, v)), 3.0);
    float spec = pow(saturate(dot(reflect(-l, n), v)), 48.0);

    // A thin glowing seam where the desktop meets the planet body.
    float2 edgePixels = min(i.uv, 1.0 - i.uv) / max(fwidth(i.uv), 1e-6);
    float seam = saturate(1.5 - min(edgePixels.x, edgePixels.y));

    // Every effect is scaled by m, so at m = 0 this is exactly the captured pixel.
    float3 lit = color * lerp(1.0, diffuse, m * 0.8);
    lit += gTint.rgb * rim * m * (0.8 + 0.9 * gHover);
    lit += gTint.rgb * seam * m * 0.9;
    lit += spec * 0.25 * m;
    return float4(lit, 1.0);
}
