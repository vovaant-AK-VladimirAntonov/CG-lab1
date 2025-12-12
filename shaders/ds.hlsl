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
  float gNoiseSeed; // Random seed for different terrain each run
};

Texture2D heightMap : register(t0);
SamplerState heightSampler : register(s0);

// Perlin noise implementation
float2 hash2(float2 p)
{
  p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
  return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float perlinNoise(float2 p)
{
  float2 i = floor(p);
  float2 f = frac(p);
  
  // Quintic interpolation curve
  float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  
  // Four corners
  float2 ga = hash2(i + float2(0.0, 0.0));
  float2 gb = hash2(i + float2(1.0, 0.0));
  float2 gc = hash2(i + float2(0.0, 1.0));
  float2 gd = hash2(i + float2(1.0, 1.0));
  
  float va = dot(ga, f - float2(0.0, 0.0));
  float vb = dot(gb, f - float2(1.0, 0.0));
  float vc = dot(gc, f - float2(0.0, 1.0));
  float vd = dot(gd, f - float2(1.0, 1.0));
  
  return lerp(lerp(va, vb, u.x), lerp(vc, vd, u.x), u.y);
}

// Fractal Brownian Motion (fBm) for more natural terrain
float fbm(float2 p, int octaves, float lacunarity, float gain)
{
  float sum = 0.0;
  float amplitude = 1.0;
  float frequency = 1.0;
  float maxValue = 0.0;
  
  for (int i = 0; i < octaves; i++)
  {
    sum += amplitude * perlinNoise(p * frequency);
    maxValue += amplitude;
    amplitude *= gain;
    frequency *= lacunarity;
  }
  
  return sum / maxValue; // Normalize to [-1, 1]
}

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

  // Generate height from Perlin noise instead of heightmap
  // Use world position + seed offset for noise sampling (different terrain each run)
  float2 noiseCoord = (output.positionWorld.xz + gNoiseSeed) * 0.005; // Scale factor for noise frequency
  
  // fBm parameters: octaves, lacunarity (frequency multiplier), gain (amplitude multiplier)
  float height = fbm(noiseCoord, 6, 2.0, 0.5);
  height = height * 0.5 + 0.5; // Remap from [-1,1] to [0,1]
  output.positionWorld.y = height * gHeightScale;

  // Calculate normal from noise derivatives
  float delta = 0.5; // World space delta for normal calculation
  float2 coordL = (output.positionWorld.xz + gNoiseSeed + float2(-delta, 0)) * 0.005;
  float2 coordR = (output.positionWorld.xz + gNoiseSeed + float2(delta, 0)) * 0.005;
  float2 coordD = (output.positionWorld.xz + gNoiseSeed + float2(0, delta)) * 0.005;
  float2 coordU = (output.positionWorld.xz + gNoiseSeed + float2(0, -delta)) * 0.005;
  
  float heightL = (fbm(coordL, 6, 2.0, 0.5) * 0.5 + 0.5) * gHeightScale;
  float heightR = (fbm(coordR, 6, 2.0, 0.5) * 0.5 + 0.5) * gHeightScale;
  float heightD = (fbm(coordD, 6, 2.0, 0.5) * 0.5 + 0.5) * gHeightScale;
  float heightU = (fbm(coordU, 6, 2.0, 0.5) * 0.5 + 0.5) * gHeightScale;
  
  float3 tangent = normalize(float3(2.0 * delta, heightR - heightL, 0));
  float3 bitangent = normalize(float3(0, heightU - heightD, 2.0 * delta));
  output.normal = normalize(cross(bitangent, tangent));

  output.positionProjection = mul(float4(output.positionWorld, 1.0f), gView);
  output.positionProjection = mul(output.positionProjection, gProj);

  return output;
}