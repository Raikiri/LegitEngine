#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform JumpFillData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  float time;
  int jumpDist;
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

layout(location = 0) out uint resPointIndex;

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
  /*if(jumpDist <= 1)
  discard;*/
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);
  vec2 centerScreenCoord = gl_FragCoord.xy / viewportSize.xy;

  
  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);

  float splatWorldRadius = 0.1f;

  uint closestPointIndex = 0;
  float closestDist = 1e7f;

  closestPointIndex = texelFetch(pointIndicesSampler, centerPixelCoord, 0).r;
  if(closestPointIndex != 0)
  {
    Point centerPoint = pointData[closestPointIndex];
    closestDist = RaySplatIntersect(rayOrigin, rayDir, centerPoint.worldPos.xyz, centerPoint.worldNormal.xyz, splatWorldRadius).dist;
  }
  
  
  ivec2 offsets[] = 
  {
    {-1, 0},
    {0, -1},
    {1, 0},
    {0, 1},
    {-1, -1},
    {1, -1},
    {1, 1},
    {-1, 1}
  };
  
  for(int offsetIndex = 0; offsetIndex < offsets.length(); offsetIndex++)
  {
    ivec2 samplePixelCoord = centerPixelCoord + offsets[offsetIndex] * jumpDist;
    uint samplePointIndex = texelFetch(pointIndicesSampler, samplePixelCoord, 0).r;
    if(samplePointIndex <= 0)
      continue;
    //closestPointIndex = samplePointIndex;
    Point point = pointData[samplePointIndex];
    Intersection intersection = RaySplatIntersect(rayOrigin, rayDir, point.worldPos.xyz, point.worldNormal.xyz, splatWorldRadius);
    
    //mat3 basis = CreateBasis(point.worldNormal.xyz);
    mat3 basis = CreateBasis(rayDir);
    vec2 uv = (transpose(basis) * (rayOrigin + rayDir * intersection.dist - point.worldPos.xyz)).xy / splatWorldRadius + vec2(0.5f); 
    //if(intersection.dist + intersection.radiusSq * 100.0f * 0.0f < closestDist && intersection.exists)
    if(uv.x > 0.0f && uv.y > 0.0f && uv.x < 1.0f && uv.y < 1.0f && intersection.exists && intersection.dist < closestDist)
    //if(intersection.exists && intersection.dist < closestDist)
    {
      vec4 color = textureLod(brushSampler, uv * 0.5f, 0.0f);
      if(color.a > 0.5f)
      {
        closestDist = intersection.dist;
        closestPointIndex = samplePointIndex;
      }
    }
  }
  resPointIndex = closestPointIndex;
  
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