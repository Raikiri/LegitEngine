#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../Common/PointsData.decl"
layout(binding = 0, set = 0) uniform MipBuilderData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 currViewportSize;
  vec4 prevViewportSize;
  float time;
};


layout(binding = 2, set = 0) uniform usampler2D prevIndicesSampler;

layout(location = 0) in vec2 fragScreenCoord;
layout(location = 0) out uint resIndex;

vec3 Unproject(vec3 screenPos, mat4 inverseProjectionMatrix)
{
  vec4 viewPos = inverseProjectionMatrix * vec4((screenPos.xy * 2.0 - 1.0), screenPos.z, 1.0);
  viewPos /= viewPos.w;
  return viewPos.xyz;
}

vec3 Project(vec3 viewPos, mat4 projectionMatrix)
{
  vec4 normalizedDevicePos = projectionMatrix * vec4(viewPos, 1.0);
  normalizedDevicePos.xyz /= normalizedDevicePos.w;

  vec3 screenPos = vec3(normalizedDevicePos.xy * 0.5 + vec2(0.5), normalizedDevicePos.z);
  return screenPos;
}



void main() 
{
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);
  vec2 centerScreenCoord = gl_FragCoord.xy / currViewportSize.xy;

  /*uint pointIndex = texelFetch(pointIndicesSampler, centerPixelCoord, 0).r;
  Point centerPoint = pointsBuf.data[pointIndex];*/
  
  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  
  uint closestPointIndex = 0;
  float closestDist = 1e7f;
  
  ivec2 offset;
  for(offset.y = 0; offset.y < 2; offset.y++)
  {
    for(offset.x = 0; offset.x < 2; offset.x++)
    {
      ivec2 samplePixelCoord = centerPixelCoord * 2 + offset;
      uint samplePointIndex = texelFetch(prevIndicesSampler, samplePixelCoord, 0).r;
      if(samplePointIndex <= 0)
        continue;
      //closestPointIndex = samplePointIndex;
      Point point = pointsBuf.data[samplePointIndex];
      float dist = dot(point.worldPos.xyz, rayDir);
      if(dist < closestDist)
      {
        closestPointIndex = samplePointIndex;
        closestDist = dist;
      }
    }
  }
  
  resIndex = closestPointIndex;
}