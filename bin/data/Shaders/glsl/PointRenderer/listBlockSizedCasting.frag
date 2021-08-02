#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform BucketCastData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  int totalBucketsCount;
  float time;
  int framesCount;
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
  vec2 raysCount = vec2(mipInfosBuf.data[0].size.xy);
  
  vec2 centerScreenCoord = gl_FragCoord.xy / raysCount;
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);

  mat4 viewProjMatrix = passDataBuf.projMatrix * passDataBuf.viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  float rayWidth = length(
    Unproject(vec3(centerScreenCoord + vec2(1.0f / raysCount.x, 0.0f), 1.0f), invViewProjMatrix) -
    Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix));
  
  resColor = vec4(0.0f);

  uint listsCount = BuildBlockLists(centerScreenCoord);
  Heapify(/*splatLists, */listsCount);
  
  vec3 currRayLight = vec3(0.0f);
  for(uint pointIndex = AdvanceLists(/*splatLists, */listsCount); pointIndex != uint(-1); pointIndex = AdvanceLists(/*splatLists, */listsCount))
  //for(uint pointIndex = bucketsBuf.data[bucketIndex].headPointIndex; pointIndex != uint(-1); pointIndex = pointsListBuf.data[pointIndex].nextPointIndex)
  {
    float NdotL = dot(pointsBuf.data[pointIndex].worldNormal.xyz, rayDir);
    float radiusRatio = pointsBuf.data[pointIndex].worldRadius * pointsBuf.data[pointIndex].worldRadius / (rayWidth * rayWidth);
    
    //avg = (curr * count + delta0 * n) / (count + 1)
    //pointsBuf.data[pointIndex].indirectLight.rgb = (pointsBuf.data[pointIndex].indirectLight.rgb * passDataBuf.framesCount /  + currRayLight *  vec3(max(0.0f, -NdotL))/* / max(1.0f, radiusRatio)*/) / (passDataBuf.framesCount + 1);
    pointsBuf.data[pointIndex].indirectLight.rgb += currRayLight *  vec3(max(0.0f, -NdotL)) / max(1.0f, radiusRatio);
    if(NdotL > 0.0f)
    {
      currRayLight = mix(currRayLight, pointsBuf.data[pointIndex].directLight.rgb, 1.0f/*clamp(radiusRatio, 0.0f, 1.0f)*/);
    }
    pointIndex = pointsListBuf.data[pointIndex].nextPointIndex;
  }
}

