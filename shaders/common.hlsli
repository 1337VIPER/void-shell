// Constants and helpers shared by every shader.
// Layouts mirror FrameConstants and ObjectConstants in src/renderer.cpp.

cbuffer Frame : register(b0)
{
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gCamPos;
    float gTime;
    float2 gScreen;
    float gVoidAmount;
    float gFramePad0;
    float3 gCamRight;
    float gFramePad1;
    float3 gCamUp;
    float gFramePad2;
};

cbuffer Object : register(b1)
{
    float3 gCenter;
    float gRadius;
    float gMorph;
    float gAspect;
    float gLonSpan;
    float gLatSpan;
    float4 gTint;
    float gHover;
    float gPulse;
    float gAlpha;
    float gKind;
    float4 gRect;
};

float3 LightDir()
{
    return normalize(float3(-0.42, 0.62, -0.66));
}

float Hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

float Noise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = Hash13(i);
    float n100 = Hash13(i + float3(1, 0, 0));
    float n010 = Hash13(i + float3(0, 1, 0));
    float n110 = Hash13(i + float3(1, 1, 0));
    float n001 = Hash13(i + float3(0, 0, 1));
    float n101 = Hash13(i + float3(1, 0, 1));
    float n011 = Hash13(i + float3(0, 1, 1));
    float n111 = Hash13(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

float Fbm(float3 p)
{
    float sum = 0.0;
    float amplitude = 0.5;
    [unroll] for (int octave = 0; octave < 5; ++octave)
    {
        sum += amplitude * Noise3(p);
        p = p * 2.03 + 17.17;
        amplitude *= 0.5;
    }
    return sum;
}
