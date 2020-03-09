#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform PassData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  vec4 bucketViewportSize;
  float pointWorldSize;
  float time;
};

#define PointIndex uint
#define null PointIndex(-1)

struct Point
{
  vec4 worldPos;
  vec4 worldNormal;
  vec4 directLight;
  vec4 indirectLight;
  PointIndex next;
  float padding[3];
};

layout(std430, binding = 1, set = 0) buffer PointData
{
  Point pointData[];
};


struct Bucket
{
  uint pointIndex;
  uint pointsCount;
};

layout(std430, binding = 2, set = 0) buffer BucketData
{
  Bucket bucketData[];
};

layout(binding = 3, set = 0) uniform usampler2D pointIndicesSampler;
layout(binding = 4, set = 0) uniform sampler2D brushSampler;

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

vec3 hsv2rgb(vec3 c)
{
  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

uint GetListSize(PointIndex head)
{
  uint count = 0;
  PointIndex curr = head;
  int p = 0;
  while (curr != null)
  {
    p++;
    count++;
    curr = pointData[curr].next;
  }
  return count;
}



#define ListIndex uint
struct PointList
{
  PointIndex pointIndex;
  ListIndex next;
};

#define offsetsCount 9
PointList lists[offsetsCount];
ivec2 offsets[offsetsCount] = 
{
  {0, 0},
  {-1, 0},
  {0, -1},
  {1, 0},
  {0, 1},
  {-1, -1},
  {1, -1},
  {1, 1},
  {-1, 1}
};

/*ListIndex InsertList(ListIndex head, ListIndex newList, vec3 sortDir)
{
  if (head == null || dot(pointData[lists[head].pointIndex].worldPos.xyz - pointData[lists[newList].pointIndex].worldPos.xyz, sortDir) >= 0.0f)
  {
    lists[newList].next = head;
    head = newList;
  }
  else
  {
    ListIndex curr = head;
    for (; pointData[lists[curr].pointIndex].next != null && dot(pointData[lists[lists[curr].next].pointIndex ].worldPos.xyz - pointData[lists[newList].pointIndex].worldPos.xyz, sortDir) < 0.0f; curr = lists[curr].next);
    lists[newList].next = lists[curr].next;
    lists[curr].next = newList;
  }
  return head;
}*/

void main() 
{
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);
  vec2 centerScreenCoord = gl_FragCoord.xy / viewportSize.xy;
  
  ivec2 bucketBufferSize = ivec2(bucketViewportSize.x + 0.5f, bucketViewportSize.y + 0.5f);
  ivec2 bucketPixelCoord = ivec2(centerScreenCoord * bucketViewportSize.xy);

  uint pointIndex = textureLod(pointIndicesSampler, centerScreenCoord, 0.0f).r;
  
  //resColor = vec4(hsv2rgb(vec3(fract(pointIndex / 100.0f), 1.0f, 0.5f)), 1.0f);
  /*if(pointIndex != uint(0))
    resColor = vec4(pointData[pointIndex].indirectLight.rgb + pointData[pointIndex].directLight.rgb, 1.0f);
  else
    resColor = vec4(0.03f);*/
  vec3 totalLight = pointData[pointIndex].indirectLight.rgb + pointData[pointIndex].directLight.rgb;
    
  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  
  Point point = pointData[pointIndex];
  Intersection intersection = RaySplatIntersect(rayOrigin, rayDir, point.worldPos.xyz, rayDir, pointWorldSize);
  
  mat3 basis = CreateBasis(/*point.worldNormal.xyz*/ rayDir);
  vec2 uv = (transpose(basis) * (rayOrigin + rayDir * intersection.dist - point.worldPos.xyz)).xy / pointWorldSize + vec2(0.5f); 
  resColor = vec4(textureLod(brushSampler, uv * 0.5f, 0.0f).rgb * totalLight, 1.0f);
  /*if(bucketPixelCoord.x >= 0 && bucketPixelCoord.y >= 0 && bucketPixelCoord.x < bucketBufferSize.x && bucketPixelCoord.y < bucketBufferSize.y)
  {
    Bucket bucket = bucketData[bucketPixelCoord.x + bucketPixelCoord.y * bucketBufferSize.x];
    //resColor = vec4(fract(bucket.pointIndex / 100.0f));
    
    resColor = vec4(hsv2rgb(vec3(fract(GetListSize(bucket.pointIndex) * 0.1f), 1.0f, 0.5f)), 1.0f);
    //resColor = vec4(hsv2rgb(vec3((bucket.pointsCount * 0.1f), 1.0f, 0.5f)), 1.0f);
    //resColor = (GetListSize(bucket.pointIndex) == bucket.pointsCount) ? vec4(bucket.pointsCount / 1000.0f) : vec4(1.0f, 0.0f, 0.0f, 1.0f);
    //resColor = vec4(bucket.pointsCount / 10009.0f);
    //resColor = vec4(fract(bucketData[0].pointsCount / 10009.0f));
    //resColor = vec4(GetListSize(bucket.pointIndex) / 100.0f + 0.1f);
  }*/
  //resColor = vec4(hsv2rgb(vec3(fract(bucket.pointIndex * 0.1f), 1.0f, 0.5f)), 1.0f);
  /*mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);

  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  
  resColor = vec4(0.03f);// : vec4(hsv2rgb(vec3(fract(pointIndex * 0.01f), 1.0f, 0.5f)), 1.0f);
  
  float splatWorldRadius = 0.1f;

  uint pointIndex = texelFetch(pointIndicesSampler, centerPixelCoord, 0).r;
  if(pointIndex != 0)
  {
    Point centerPoint = pointData[pointIndex];
    //resColor = centerPoint.directLight;

    Intersection intersection = RaySplatIntersect(rayOrigin, rayDir, centerPoint.worldPos.xyz, centerPoint.worldNormal.xyz, splatWorldRadius);
    
    mat3 basis = CreateBasis(rayDir);
    vec2 uv = (transpose(basis) * (rayOrigin + rayDir * intersection.dist - centerPoint.worldPos.xyz)).xy / splatWorldRadius + vec2(0.5f); 
    //if(intersection.dist + intersection.radiusSq * 100.0f * 0.0f < closestDist && intersection.exists)
    vec4 brushColor = textureLod(brushSampler, uv * 0.5f, 0.0f);
    resColor = centerPoint.directLight * brushColor;
  }*/
}