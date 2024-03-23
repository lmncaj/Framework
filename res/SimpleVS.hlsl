#include "Simple.hlsli"
struct VSInput
{
    float3 Positon : POSITION;
    float4 Color : COLOR;
};


cbuffer Transform : register(b0)
{
    float4x4 World : packoffset(c0);
    float4x4 View : packoffset(c4);
    float4x4 Proj:packoffset(c8);
}
VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    float4 localPos = float4(input.Positon, 1.0f);
    float4 worldPos = mul(World, localPos);
    float4 viewPos = mul(View, worldPos);
    float4 projPos = mul(Proj, viewPos);
    output.Position = projPos;
    output.Color = input.Color;
    return output;
}
