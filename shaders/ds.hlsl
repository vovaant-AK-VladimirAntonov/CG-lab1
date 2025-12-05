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

Texture2D heightMap : register(t0);
SamplerState heightSampler : register(s0);

[domain("quad")]
PixelIn DS(PatchTess patchTess, float2 uv : SV_DomainLocation, const OutputPatch<DomainIn, 4> quad)
{
  PixelIn output;

	// Bilinear interpolation.
  output.positionWorld = lerp(
		lerp(quad[0].positionWorld, quad[1].positionWorld, uv.x),
		lerp(quad[2].positionWorld, quad[3].positionWorld, uv.x),
		uv.y);

  // Global texture coordinates for heightmap
  output.texCoord = lerp(
		lerp(quad[0].texCoord, quad[1].texCoord, uv.x),
		lerp(quad[2].texCoord, quad[3].texCoord, uv.x),
		uv.y);

  // Local texture coordinates for color texture
  output.localTexCoord = lerp(
		lerp(quad[0].localTexCoord, quad[1].localTexCoord, uv.x),
		lerp(quad[2].localTexCoord, quad[3].localTexCoord, uv.x),
		uv.y);

  // Sample heightmap and displace vertex
  float height = heightMap.SampleLevel(heightSampler, output.texCoord, 0).r;
  output.positionWorld.y = height * gHeightScale; // Use height scale from constant buffer

  // Calculate normal from heightmap
  float texelSize = 1.0f / 512.0f; // Heightmap resolution
  
  float heightL = heightMap.SampleLevel(heightSampler, output.texCoord + float2(-texelSize, 0), 0).r * gHeightScale;
  float heightR = heightMap.SampleLevel(heightSampler, output.texCoord + float2(texelSize, 0), 0).r * gHeightScale;
  float heightD = heightMap.SampleLevel(heightSampler, output.texCoord + float2(0, texelSize), 0).r * gHeightScale;
  float heightU = heightMap.SampleLevel(heightSampler, output.texCoord + float2(0, -texelSize), 0).r * gHeightScale;
  
  float3 tangent = normalize(float3(2.0f, heightR - heightL, 0));
  float3 bitangent = normalize(float3(0, heightU - heightD, 2.0f));
  output.normal = normalize(cross(bitangent, tangent));

  output.positionProjection = mul(float4(output.positionWorld, 1.0f), gView);
  output.positionProjection = mul(output.positionProjection, gProj);

  return output;
}