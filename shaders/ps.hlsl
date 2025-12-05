#include "Common.h"

Texture2D colorTexture : register(t1);  // Color texture at t1 (heightmap is at t0)
SamplerState samplerState : register(s0);

float4 PS(PixelIn input) : SV_TARGET
{
  // Sample the color texture using local coordinates
  float4 textureColor = colorTexture.Sample(samplerState, input.localTexCoord);
  
  // Use normal from domain shader (calculated from heightmap)
  float3 normal = normalize(input.normal);
  
  // Directional lighting
  float3 lightDirection = normalize(float3(0.3f, -1.0f, 0.3f));
  
  // Diffuse lighting
  float diffuse = saturate(dot(normal, -lightDirection));
  
  // Ambient light
  float ambient = 0.4f;
  
  // Combine lighting
  float lightIntensity = ambient + diffuse * 0.6f;
  
  float4 finalColor = textureColor * lightIntensity;
  finalColor.a = 1.0f;
  
  return finalColor;
}
