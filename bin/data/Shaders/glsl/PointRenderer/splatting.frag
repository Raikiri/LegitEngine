#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform SplattingData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  float time;
};

struct Point
{
  vec4 worldPos;
  vec4 worldNormal;
  vec4 directLight;
  vec4 indirectLight;
  uint next;
  float padding[3];
};

layout(std430, binding = 1, set = 0) buffer PointData
{
	Point pointData[];
};

layout(binding = 2, set = 0) uniform usampler2D pointIndicesSampler;
layout(binding = 3, set = 0) uniform sampler2D brushSampler;

layout(location = 0) in vec2 fragScreenCoord;

layout(location = 0) out vec4 resColor;

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
float saturate(float val)
{
  return clamp(val, 0.0f, 1.0f);
}

float rand(vec2 co){
  return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}


// Low discrepancy on [0, 1] ^2
vec2 HammersleyNorm(int i, int N)
{
  // principle: reverse bit sequence of i
  uint b =  ( uint(i) << 16u) | (uint(i) >> 16u );
  b = (b & 0x55555555u) << 1u | (b & 0xAAAAAAAAu) >> 1u;
  b = (b & 0x33333333u) << 2u | (b & 0xCCCCCCCCu) >> 2u;
  b = (b & 0x0F0F0F0Fu) << 4u | (b & 0xF0F0F0F0u) >> 4u;
  b = (b & 0x00FF00FFu) << 8u | (b & 0xFF00FF00u) >> 8u;

  return vec2( i, b ) / vec2( N, 0xffffffffU );
}

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
void main() 
{
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);
  vec2 centerScreenCoord = gl_FragCoord.xy / viewportSize.xy;

  /*uint pointIndex = texelFetch(pointIndicesSampler, centerPixelCoord, 0).r;
  Point centerPoint = pointData[pointIndex];*/
  
  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);

  resColor = vec4(0.0f);  
  uint closestPointIndex = 0;
  float closestDist = 1e7f;
  
  float splatWorldRadius = 0.02f;
  
  struct StackEntry
  {
    uint pointIndex;
    vec2 uv;
    float dist;
  };
  #define maxStackSize 10
  int stackSize = 0;
  StackEntry stack[maxStackSize];
  
  int pixelRadius = 1;
  ivec2 offset;
  for(int level = 0; level < 1; level++)
  {
    int mult = 1 << level;
    for(offset.y = -pixelRadius; offset.y <= pixelRadius; offset.y++)
    {
      for(offset.x = -pixelRadius; offset.x <= pixelRadius; offset.x++)
      {
        ivec2 samplePixelCoord = centerPixelCoord / mult + offset;
        uint samplePointIndex = texelFetch(pointIndicesSampler, samplePixelCoord, level).r;
        if(samplePointIndex <= 0)
          continue;
        //closestPointIndex = samplePointIndex;
        Point point = pointData[samplePointIndex];
        Intersection intersection = RaySplatIntersect(rayOrigin, rayDir, point.worldPos.xyz, point.worldNormal.xyz, splatWorldRadius);
        
        //mat3 basis = CreateBasis(point.worldNormal.xyz);
        mat3 basis = CreateBasis(rayDir);
        vec2 uv = (transpose(basis) * (rayOrigin + rayDir * intersection.dist - point.worldPos.xyz)).xy / splatWorldRadius + vec2(0.5f); 
        //if(intersection.dist + intersection.radiusSq * 100.0f * 0.0f < closestDist && intersection.exists)
        if(uv.x > 0.0f && uv.y > 0.0f && uv.x < 1.0f && uv.y < 1.0f && intersection.exists)
        {
          bool found = false;
          for(int i = 0; i < stackSize; i++)
            if(stack[i].pointIndex == samplePointIndex)
              found = true;
          if(found)
            continue;
            
          int newSize = min(stackSize + 1, maxStackSize - 1);
          stack[newSize - 1].pointIndex = samplePointIndex;
          stack[newSize - 1].dist = intersection.dist;
          stack[newSize - 1].uv = uv;
          stackSize = newSize;
          
          for(int i = newSize - 1; i > 0; i--)
          {
            if(stack[i].dist < stack[i - 1].dist)
            {
              StackEntry tmp = stack[i];
              stack[i] = stack[i - 1];
              stack[i - 1] = tmp;
            }
          }
          /*if(intersection.dist < closestDist)
          {
            closestPointIndex = samplePointIndex;
            closestDist = intersection.dist;
            
            closestDist = mix(closestDist, intersection.dist, splatColor.a);  
            resColor = vec4(mix(resColor.rgb, splatColor.rgb, splatColor.a), resColor.a + (1.0f - resColor.a) * splatColor.a);
          }else
          {
            closestDist = mix(intersection.dist, closestDist, resColor.a);  
            resColor = vec4(mix(splatColor.rgb, resColor.rgb, resColor.a), splatColor.a + (1.0f - splatColor.a) * resColor.a);
          }*/
        }
      }
    }
  }
  for(int i = stackSize - 1; i >= 0; i--)
  {
    int tileIndex = int(4.0f * rand(vec2(stack[i].pointIndex, 0)));
    vec2 tileCoord = vec2(tileIndex % 2, tileIndex / 2) / 2.0f;
    vec4 splatColor = textureLod(brushSampler, tileCoord + stack[i].uv * 0.5f, 0.0f);
    Point point = pointData[stack[i].pointIndex];
    splatColor.rgb *= point.directLight.rgb + vec3(0.1f);
    //resColor = vec4(mix(resColor.rgb, splatColor.rgb, splatColor.a), resColor.a + (1.0f - resColor.a) * splatColor.a);
    resColor = vec4(mix(resColor.rgb, splatColor.rgb, splatColor.a), resColor.a + (1.0f - resColor.a) * splatColor.a);
  }

  
  /*if(closestPointIndex != 0)
  {
    Point point = pointData[closestPointIndex];
    //resColor = vec4(point.worldNormal.xyz, 1.0f);
    float exposure = 1.0f;
    resColor = vec4(1.0f - exp(-exposure * point.directLight.rgb), 1.0f);
  }else
  {
    resColor = vec4(vec3(0.2f), 1.0f);
  }*/
  //resColor = textureLod(brushSampler, centerScreenCoord, 1.5f) * 2.0f;
}