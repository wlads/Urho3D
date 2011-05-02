#include "Common.hlsl"

void VS(float4 iPos : POSITION,
    out float4 oPos : POSITION,
    out float3 oTexCoord : TEXCOORD0)
{
    GetPosition(iPos, oPos);
    oTexCoord = iPos.xyz;
}

void PS(float3 iTexCoord : TEXCOORD0,
    out float4 oColor : COLOR0,
    out float oDepth : DEPTH0)
{
    oColor = cMatDiffColor * texCUBE(sDiffCubeMap, iTexCoord);
    oDepth = 1.0;
}
