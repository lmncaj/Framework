#include "Simple.hlsli"
struct PSOutput
{
    float4 Color : SV_TARGET0;
};

PSOutput main(VSOutput input)
{
    PSOutput output = (PSOutput)0;
    output.Color = input.Color;
    return output;
}