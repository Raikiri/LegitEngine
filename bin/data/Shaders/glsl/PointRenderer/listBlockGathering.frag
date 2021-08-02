#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform PassDataBuffer
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  uint bucketGroupsCount;
  float time;
  uint framesCount;
  int debugMip;
  int debugType;
} passDataBuf;

#include "../Common/projection.decl"
#include "../Common/ListBucketeer/bucketsData.decl"
#include "../Common/ListBucketeer/pointsListData.decl"
#include "../Common/ListBucketeer/blockPointsListData.decl"
#include "../Common/pointsData.decl"
#include "PointSplatting.decl"


layout(location = 0) in vec2 fragScreenCoord;
layout(location = 0) out vec4 resColor;

void main() 
{
  vec2 tileOffsets[] = vec2[](vec2(0, 0), vec2(0.5f, 0), vec2(0, 0.5f), vec2(0.5f, 0.5f));

  vec2 centerScreenCoord = gl_FragCoord.xy / passDataBuf.viewportSize.xy;
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);

  mat4 viewProjMatrix = passDataBuf.projMatrix * passDataBuf.viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  
  uint mipsCount = mipInfosBuf.data.length();

  uint pointsCount = 0;

  resColor = vec4(0.0f);

  for(int mipNumber = 0; mipNumber < mipsCount; mipNumber++)
  {
    uint mipLevel = mipsCount - 1 - mipNumber;
    //uint mipIndex = 2;
    ivec2 bucketCoord = GetBucketClampedCoord(centerScreenCoord, mipLevel, vec2(0.0f));
    uint bucketIndex = GetBucketIndexSafe(bucketCoord, mipLevel);
    
    /*for(uint bucketNumber = 0; bucketNumber < 4; bucketNumber++)
    {
      uint bucketIndex = bucketIndices[bucketNumber];
      if(bucketIndex == uint(-1))
        continue;
        
      pointsCount += bucketsBuf.data[bucketIndex].pointsCount;

      for(uint pointIndex = bucketsBuf.data[bucketIndex].headPointIndex; pointIndex != uint(-1); pointIndex = pointsBuf.data[pointIndex].nextPointIndex)
      {
      }
    }*/
    /*uint patternNumber = 0;
    while(patternNumber < 4 && bucketIndices[patternNumber] == uint(-1))
    {
      patternNumber++;
    }*/
    //if(patternNumber >= 4) continue;
    //uint bucketIndex = bucketIndices[patternNumber];
    if(bucketIndex == uint(-1)) continue;
    BucketLocation bucketLocation = GetBucketLocation(bucketIndex);
    uint listNumber = GetBoxListNumber(bucketLocation.localCoord);
      
    for(uint pointIndex = bucketsBuf.data[bucketIndex].blockHeadPointIndex; pointIndex != uint(-1); pointIndex = blockPointsListBuf.data[pointIndex].lists[listNumber].nextPointIndex)
    //for(uint pointIndex = bucketsBuf.data[bucketIndex].headPointIndex; pointIndex != uint(-1); pointIndex = pointsListBuf.data[pointIndex].nextPointIndex)
    {
      SplatPoint(rayOrigin, rayDir, pointIndex, passDataBuf.framesCount, resColor);
      if(resColor.a > 0.9f)
        return;
    }
  }
}

