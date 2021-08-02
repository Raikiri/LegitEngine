#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable

#include "../passData.decl"
#include "../../projection.decl"
#include "../bucketsData.decl"
#include "../../pointsData.decl"
#include "../pointsListData.decl"

layout(location = 0) in flat uint fragPointIndex;

void main() 
{
  vec2 screenCoord = gl_FragCoord.xy / vec2(mipInfosBuf.data[0].size.xy);
  float floatMipLevel = GetPointMipLevel(pointsBuf.data[fragPointIndex].worldPos.xyz, pointsBuf.data[fragPointIndex].worldRadius);
  /*if(floatMipLevel < 1e-3f)
    return;*/
    
  int mipLevel = int(floatMipLevel + 0.5f);
  ivec2 clampedCoord = GetBucketClampedCoord(screenCoord, mipLevel, vec2(0.5f));
  uint bucketIndex = GetBucketIndexSafe(clampedCoord, mipLevel);

  if(bucketIndex != uint(-1))
  {
    uint prevHeadPointIndex = atomicExchange(bucketsBuf.data[bucketIndex].headPointIndex, fragPointIndex);
    if(pointsListBuf.data[fragPointIndex].nextPointIndex != uint(-1))
      mipInfosBuf.data[0].debug += 1.0f;
    pointsListBuf.data[fragPointIndex].nextPointIndex = prevHeadPointIndex;
    atomicAdd(bucketsBuf.data[bucketIndex].pointsCount, 1);
  }
}