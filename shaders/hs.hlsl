#include "Common.h"

cbuffer PassConstants : register(b0)
{
  matrix gView;
  matrix gProj;
  matrix gViewProj;
  float3 gEyePosW;
  float gPadding1;
  float2 gRenderTargetSize;
  float gNearZ;
  float gFarZ;
  float gTotalTime;
  float gDeltaTime;
  float gHeightScale;
  float gPadding2;
};

// LOD параметры - настрой под свои нужды
static float gMinDist = 500;     // До этого расстояния - максимальная детализация
static float gMaxDist = 2500;    // После этого - минимальная детализация (быстрый переход)

static float gMinTess = 1;      // Минимальная тесселяция (1 сегмент = 2 треугольника на патч)
static float gMaxTess = 6;      // Максимальная тесселяция (64 сегмента)

float CalcTessFactor(float3 p)
{
  float d = distance(p, gEyePosW);
  float s = saturate((d - gMinDist) / (gMaxDist - gMinDist));

  // Lerp between max and min tessellation based on distance
  // Close = high tess (64), Far = low tess (1)
  float tess = pow(2, lerp(gMaxTess, gMinTess, s));
  
  // Minimum tessellation is 1
  return max(1.0f, tess);
}

PatchTess ConstantHS(InputPatch<HullIn, 4> patch, uint patchID : SV_PrimitiveID)
{
  PatchTess pt;

  // Calculate edge midpoints for LOD calculation
  float3 e0 = 0.5f * (patch[0].positionWorld + patch[2].positionWorld); // Left
  float3 e1 = 0.5f * (patch[0].positionWorld + patch[1].positionWorld); // Bottom
  float3 e2 = 0.5f * (patch[1].positionWorld + patch[3].positionWorld); // Right
  float3 e3 = 0.5f * (patch[2].positionWorld + patch[3].positionWorld); // Top
  float3 c = 0.25f * (patch[0].positionWorld + patch[1].positionWorld + patch[2].positionWorld + patch[3].positionWorld);

  // Calculate tessellation for each edge
  pt.edgeTess[0] = CalcTessFactor(e0);
  pt.edgeTess[1] = CalcTessFactor(e1);
  pt.edgeTess[2] = CalcTessFactor(e2);
  pt.edgeTess[3] = CalcTessFactor(e3);

  pt.insideTess[0] = CalcTessFactor(c);
  pt.insideTess[1] = pt.insideTess[0];

  return pt;
}

[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0f)]
DomainIn HS(InputPatch<HullIn, 4> p, uint i : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
  DomainIn output;

  output.positionWorld = p[i].positionWorld;
  output.texCoord = p[i].texCoord;
  output.localTexCoord = p[i].localTexCoord;

  return output;
}
