#include "Common.h"

HullIn VS(VertexIn input)
{
  HullIn output;

  output.positionWorld = input.positionWorld.xyz;
  output.texCoord = input.texCoord;           // Global coords for heightmap
  output.localTexCoord = input.localTexCoord; // Local coords for color texture

  return output;
}