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
  bool exists;
};
Intersection RaySplatIntersect(vec3 rayOrigin, vec3 rayDir, vec3 splatPoint, vec3 splatNormal, float splatRadius)
{
  float param = -dot(rayOrigin - splatPoint, splatNormal) / dot(rayDir, splatNormal);
  vec3 point = rayOrigin + rayDir * param;
  vec3 delta = point - splatPoint;
  Intersection res;
  res.dist = param;
  res.exists = (dot(delta, delta) < splatRadius * splatRadius) && (param > 0.0f);
  return res;
}

void main() 
{
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);
  vec2 centerScreenCoord = gl_FragCoord.xy / viewportSize.xy;

  uint pointIndex = texelFetch(pointIndicesSampler, centerPixelCoord, 0).r;
  if(pointIndex == 0)
    discard;

  Point centerPoint = pointData[pointIndex];
  
  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  
  uint closestPointIndex = 0;
  float closestDist = 1e7f;
  
  float splatWorldRadius = 0.1f;
  
  vec3 sourceIntensity = vec3(2.0f) * 1.0f / (length(rayOrigin - centerPoint.worldPos.xyz) + 1e-7f);
  vec3 lightAmount = sourceIntensity * max(0.0f, -dot(centerPoint.worldNormal.xyz, rayDir));
  
  int pixelRadius = 1;
  ivec2 offset;
  
  for(int level = 0; level < 6; level++)
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
        Intersection intersection = RaySplatIntersect(centerPoint.worldPos.xyz, -rayDir, point.worldPos.xyz, point.worldNormal.xyz, splatWorldRadius);
        if(intersection.dist > 0.02f && intersection.exists)
        {
          lightAmount *= 0.0f;
          closestPointIndex = samplePointIndex;
          closestDist = intersection.dist;
        }
      }
    }
  }
  resColor = vec4(1.0f, 0.5f, 0.0f, 1.0f);
  pointData[pointIndex].directLight += vec4(lightAmount, 0.0f);
}