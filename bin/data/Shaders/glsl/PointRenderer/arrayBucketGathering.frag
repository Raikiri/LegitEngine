#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform PassData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  uint bucketGroupsCount;
  float time;
}passDataBuf;

#include "../Common/projection.decl"
#include "../Common/ArrayBucketeer/bucketsData.decl"
#include "../Common/pointsData.decl"
#include "../Common/ArrayBucketeer/bucketGroupsData.decl"

layout(binding = 7, set = 0) uniform sampler2D brushSampler;

struct Intersection
{
  float dist;
  float radiusSq;
  bool exists;
};
Intersection RaySplatIntersect(vec3 rayOrigin, vec3 rayDir, vec3 splatPoint, vec3 splatNormal, float splatRadius)
{
  float param = -dot(rayOrigin - splatPoint, splatNormal) / dot(rayDir, splatNormal);
  vec3 point = rayOrigin + rayDir * param;
  vec3 delta = point - splatPoint;
  Intersection res;
  res.dist = param;
  res.radiusSq = dot(delta, delta);
  res.exists = /*(res.radiusSq < splatRadius * splatRadius) && */(param > 0.0f);
  return res;
}

mat3 CreateBasis(vec3 normal)
{
  mat3 res;
  res[0] = normalize(cross(normal, vec3(0.0f, 1.0f, 0.1f)));
  res[1] = cross(normal, res[0]);
  res[2] = normal;
  return res;
}

vec3 hsv2rgb(vec3 c)
{
  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float rand(vec2 co){
  return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

mat2 Rotation2(float ang)
{
  float cosa = cos(ang);
  float sina = sin(ang);
  return mat2(cosa, sina, -sina, cosa);
}
layout(location = 0) in vec2 fragScreenCoord;
layout(location = 0) out vec4 resColor;

vec4 GetDebugColor(float val)
{
  float hue = 1.0f / (1.0f + val) * 0.9f;
  return vec4(hsv2rgb(vec3(hue, 1.0f, 1.0f)), 1.0f);
}
uint AdvanceArrays(inout uvec4 entryIndices)
{
  float maxDist = 1e7f;
  uint bestCoord = uint(-1);
  uint bestPointIndex = uint(-1);
  for(int i = 0; i < 4; i++)
  {
    BucketEntry entry;
    if(entryIndices[i] != uint(-1) && (entry = bucketEntriesPoolBuf.data[entryIndices[i]]).pointDist < maxDist)
    {
      maxDist = entry.pointDist;
      bestPointIndex = entry.pointIndex;
      bestCoord = i;
    }
  }
  
  if(bestCoord != uint(-1))
  {
    //bestPointIndex = bucketEntriesPoolBuf.data[entryIndices[bestCoord]].pointIndex;
    entryIndices[bestCoord]++;
    
    /*if(bucketEntriesPoolBuf.data[entryIndices[bestCoord]].pointIndex == uint(-1))
    {
      entryIndices[bestCoord] = uint(-1);
    }*/
  }
    
  return bestPointIndex;
}

void main() 
{
  //Bucket buckets[4];
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
  /*float bucketOffset = bucketGroups[0].bucketIndexGlobalOffset;
  resColor = centerScreenCoord.y > 0.5f ? GetDebugColor(bucketOffset) : GetDebugColor(centerScreenCoord.x * 100.0f);*/
  for(int mipNumber = 0; mipNumber < mipsCount; mipNumber++)
  {
    uint mipIndex = mipsCount - 1 - mipNumber;
    //uint mipIndex = 2;
    uvec4 bucketIndices = GetBucketBlockIndices(centerScreenCoord, mipIndex, vec2(-0.5f));
    uvec4 entryIndices;
    for(uint bucketNumber = 0; bucketNumber < 4; bucketNumber++)
    {
      entryIndices[bucketNumber] = uint(-1);
      if(bucketIndices[bucketNumber] != uint(-1))
      {
        entryIndices[bucketNumber] = bucketsBuf.data[bucketIndices[bucketNumber]].entryOffset;
      }
    }
    //uint bucketIndex = GetBucketIndex(centerScreenCoord, mipIndex);
    //pointsCount += bucketsBuf.data[bucketIndex].pointsCount;

    //for(int pointNumber = 0; pointNumber < min(1000, bucketsBuf.data[bucketIndex].pointsCount); pointNumber++)
    for(uint pointIndex = AdvanceArrays(entryIndices); pointIndex != uint(-1); pointIndex = AdvanceArrays(entryIndices))
    {
      //uint pointIndex = bucketEntriesPoolBuf.data[bucketsBuf.data[bucketIndex].indexOffset + bucketsBuf.data[bucketIndex].pointsCount - 1 - pointNumber].pointIndex;*/
      Intersection intersection = RaySplatIntersect(rayOrigin, rayDir, pointsBuf.data[pointIndex].worldPos.xyz, rayDir, pointsBuf.data[pointIndex].worldRadius); ;//point.worldNormal.xyz
    
      mat3 basis = CreateBasis(rayDir);//point.worldNormal.xyz;
      vec3 intersectionPoint = rayOrigin + rayDir * intersection.dist;
      vec2 uv = (transpose(basis) * (intersectionPoint - pointsBuf.data[pointIndex].worldPos.xyz)).xy / pointsBuf.data[pointIndex].worldRadius + vec2(0.5f); 
      
      mat2 rotation = Rotation2(rand(vec2(pointIndex / 1000.0f, 0.0f)) * 2.0f * 3.1415f);
      uv = rotation * (uv - vec2(0.5)) + vec2(0.5);
      if(uv.x > 0.0f && uv.y > 0.0f && uv.x < 1.0f && uv.y < 1.0f && intersection.exists)
      {
        uint tileIndex = int(rand(vec2(pointIndex / 1000.0f, 0.1f)) * 3.99f);
        vec2 tileOffset = tileOffsets[tileIndex];
        vec4 color = texture(brushSampler, uv * 0.5f + tileOffset);
        vec3 totalLight = max(vec3(0.0), pointsBuf.data[pointIndex].indirectLight.rgb + pointsBuf.data[pointIndex].directLight.rgb);// + vec3(0.1f);
        color.a *= 0.9f;
        resColor.rgb += (1.0f - resColor.a) * color.rgb * totalLight * color.a * 10.1;
        resColor.a += (1.0f - resColor.a) * color.a;
        if(resColor.a > 0.9f)
          return;
      }
    }
  }
}