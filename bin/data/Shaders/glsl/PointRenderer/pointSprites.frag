#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform PassData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  float pointWorldSize;
  float fovy;
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
layout(binding = 2, set = 0) uniform sampler2D brushSampler;


layout(binding = 0, set = 1) uniform DrawCallData
{
  mat4 modelMatrix; //object->world
  vec4 albedoColor;
  vec4 emissiveColor;
  int basePointIndex;
};


layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragUv;
layout(location = 3) in flat uint  fragPointIndex;

/*layout(location = 0) out vec4 outAlbedoColor;
layout(location = 1) out vec4 outEmissiveColor;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outDepth;*/
layout(location = 0) out uint outPointIndex;
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

float rand(vec3 co){
  return fract(sin(dot(co.xyz ,vec3(12.9898,78.233, 143.8533))) * 43758.5453);
}

void main() 
{
  ivec2 centerPixelCoord = ivec2(gl_FragCoord.xy);
  vec2 centerScreenCoord = gl_FragCoord.xy / viewportSize.xy;

  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  
  Point point = pointData[fragPointIndex];
  Intersection intersection = RaySplatIntersect(rayOrigin, rayDir, point.worldPos.xyz, rayDir /*point.worldNormal.xyz*/, pointWorldSize);
  
  mat3 basis = CreateBasis(rayDir/*point.worldNormal.xyz*/);
  vec3 intersectionPoint = rayOrigin + rayDir * intersection.dist;
  vec2 uv = (transpose(basis) * (intersectionPoint - point.worldPos.xyz)).xy / pointWorldSize + vec2(0.5f); 
  vec4 color = textureLod(brushSampler, uv * 0.5f, 0.0f);
  float alphaRand = 0.5f;//rand(centerScreenCoord, float(fragPointIndex));
  if(color.a > alphaRand && uv.x > 0.0f && uv.y > 0.0f && uv.x < 1.0f && uv.y < 1.0f && intersection.exists)
  {
    outPointIndex = fragPointIndex;
    //gl_FragDepth = Project(intersectionPoint, viewProjMatrix).z;
  }else
  {
    discard;
  }
}