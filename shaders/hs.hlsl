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

static float gMinDist = 100;
static float gMaxDist = 1000;

static float gMinTess = 1;
static float gMaxTess = 6;

float CalcTessFactor(float3 p)
{
  float d = distance(p, gEyePosW);
  float s = saturate((d - gMinDist) / (gMaxDist - gMinDist));

  return pow(2, (lerp(gMaxTess, gMinTess, s)));
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

  // Check if patch is on tile boundary (every 512 units)
  bool onBoundary = (fmod(patch[0].positionWorld.x, 512.0f) < 1.0f) || 
                    (fmod(patch[0].positionWorld.z, 512.0f) < 1.0f) ||
                    (fmod(patch[3].positionWorld.x, 512.0f) < 1.0f) || 
                    (fmod(patch[3].positionWorld.z, 512.0f) < 1.0f);

  float baseTess0 = CalcTessFactor(e0);
  float baseTess1 = CalcTessFactor(e1);
  float baseTess2 = CalcTessFactor(e2);
  float baseTess3 = CalcTessFactor(e3);

  // Increase tessellation on boundaries to hide seams
  float boundaryMultiplier = onBoundary ? 2.0f : 1.0f;

  pt.edgeTess[0] = baseTess0 * boundaryMultiplier;
  pt.edgeTess[1] = baseTess1 * boundaryMultiplier;
  pt.edgeTess[2] = baseTess2 * boundaryMultiplier;
  pt.edgeTess[3] = baseTess3 * boundaryMultiplier;

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
