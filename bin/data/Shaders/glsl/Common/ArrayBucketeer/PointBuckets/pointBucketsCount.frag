#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable

#include "../passData.decl"
#include "../../projection.decl"
#include "../bucketsData.decl"
#include "../../pointsData.decl"

layout(location = 0) in flat uint fragPointIndex;

void main() 
{
  uvec4 bucketIndices = GetPointBucketBlockIndices(gl_FragCoord.xy, pointsBuf.data[fragPointIndex].worldPos.xyz, pointsBuf.data[fragPointIndex].worldRadius);

  for(int i = 0; i < 1; i++)
  {
    if(bucketIndices[i] != uint(-1))
    {
      atomicAdd(bucketsBuf.data[bucketIndices[i]].pointsCount, 1);
    }
  }
}