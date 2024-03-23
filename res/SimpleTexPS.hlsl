#include "SimpleTex.hlsli"
struct PSOutput
{
    float4 Color : SV_TARGET0;
};

SamplerState ColorSmp : register(s0);
Texture2D ColorMap : register(t0);

PSOutput main(VSOutput input)
{
    PSOutput output = (PSOutput)0;
    output.Color = ColorMap.Sample(ColorSmp, input.TexCoord);
    return output;
}