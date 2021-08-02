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
#include "../Common/pointsData.decl"
#include "PointSplatting.decl"

layout(location = 0) in vec2 fragScreenCoord;
layout(location = 0) out vec4 resColor;

uint AdvanceLists(inout uvec4 pointIndices, inout vec4 pointDists, vec3 sortDir)
{
  float maxDist = 1e7f;
  uint bestCoord = uint(-1);
  for(int i = 0; i < 4; i++)
  {
    if(pointIndices[i] != uint(-1) && pointDists[i] < maxDist)
    {
      maxDist = pointDists[i];
      bestCoord = i;
    }
  }
  
  uint bestPointIndex = -1;
  if(bestCoord != uint(-1))
  {
    bestPointIndex = pointIndices[bestCoord];
    uint nextPointIndex = pointsListBuf.data[bestPointIndex].nextPointIndex;
    pointIndices[bestCoord] = nextPointIndex;
    pointDists[bestCoord] = (nextPointIndex == uint(-1)) ? 0.0f : pointsListBuf.data[nextPointIndex].dist;
  }
    
  return bestPointIndex;
}
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
  /*if(mipInfosBuf.data[0].debug > 0.0f)
  {
    resColor = GetDebugColor(mipInfosBuf.data[0].debug / 1000.0f);
    return;
  }*/
  /*float bucketOffset = bucketGroups[0].bucketIndexGlobalOffset;
  resColor = centerScreenCoord.y > 0.5f ? GetDebugColor(bucketOffset) : GetDebugColor(centerScreenCoord.x * 100.0f);*/
  for(int mipNumber = 0; mipNumber < mipsCount; mipNumber++)
  {
    uint mipIndex = mipsCount - 1 - mipNumber;
    //uint mipIndex = 2;
    uvec4 bucketIndices = GetBucketBlockIndices(centerScreenCoord, mipIndex, vec2(0.0f));
    
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
    uvec4 pointIndices;
    vec4 pointDists;
    for(uint pointNumber = 0; pointNumber < 4; pointNumber++)
    {
      uint bucketIndex = bucketIndices[pointNumber];
      uint pointIndex = ((bucketIndex == uint(-1)) ? uint(-1) : bucketsBuf.data[bucketIndex].headPointIndex);
      pointDists[pointNumber] = (pointIndex == uint(-1)) ? 0.0f : pointsListBuf.data[pointIndex].dist;
      pointIndices[pointNumber] = pointIndex;
    }
    
    for(uint pointIndex = AdvanceLists(pointIndices, pointDists, rayDir); pointIndex != uint(-1); pointIndex = AdvanceLists(pointIndices, pointDists, rayDir))
    {
      SplatPoint(rayOrigin, rayDir, pointIndex, passDataBuf.framesCount, resColor);
      if(resColor.a > 0.9f)
        return;
    }
  }
}

