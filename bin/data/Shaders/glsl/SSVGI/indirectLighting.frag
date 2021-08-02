#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform IndirectLightingData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
};

layout(binding = 1, set = 0) uniform sampler2D blurredDirectLightSampler;
layout(binding = 2, set = 0) uniform sampler2D blurredDepthMomentsSampler;
layout(binding = 3, set = 0) uniform sampler2D normalSampler;
layout(binding = 4, set = 0) uniform sampler2D depthStencilSampler;

layout(location = 0) in vec2 fragScreenCoord;

layout(location = 0) out vec4 indirectLight;

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

float ComputeHorizonContribution(vec3 eyeDir, vec3 eyeTangent, vec3 viewNorm, float minAngle, float maxAngle)
{
  return
    +0.25 * dot(eyeDir, viewNorm) * (- cos(2.0 * maxAngle) + cos(2.0 * minAngle))
    +0.25 * dot(eyeTangent, viewNorm) * (2.0 * maxAngle - 2.0 * minAngle - sin(2.0 * maxAngle) + sin(2.0 * minAngle));
}

vec2 GetAngularDistribution(vec3 centerViewPoint, vec3 eyeDir, vec3 eyeTangent, vec3 rayDir, vec2 depthDistribution)
{
  vec3 viewPoint = rayDir * depthDistribution.x;

  vec3 diff = viewPoint - centerViewPoint;
  
  float depthProj = dot(eyeDir, diff);
  float tangentProj = dot(eyeTangent, diff);
  
  float invTangentProj = 1.0f / tangentProj;
  
  float horizonRatio = depthProj * invTangentProj;
  float horizonAngle = atan(1.0f, horizonRatio);

  float horizonAngleDerivative;
  {
    float eps = 1e-1f;
    vec3 offsetViewPoint = rayDir * (depthDistribution.x + eps);
    vec3 offsetDiff = offsetViewPoint - centerViewPoint;
    
    float offsetDepthProj = dot(eyeDir, offsetDiff);
    float offsetTangentProj = dot(eyeTangent, offsetDiff);
    
    float invOffsetTangentProj = 1.0f / offsetTangentProj;
    
    float offsetHorizonRatio = offsetDepthProj * invOffsetTangentProj;
    float offsetHorizonAngle = atan(1.0f, offsetHorizonRatio);
    horizonAngleDerivative = (offsetHorizonAngle - horizonAngle) / eps;
  }
  return vec2(horizonAngle, depthDistribution.y * horizonAngleDerivative * horizonAngleDerivative);
}

bool BoxRayCast(vec2 rayStart, vec2 rayDir, vec2 boxMin, vec2 boxMax, out float paramMin, out float paramMax)
{
  // r.dir is unit direction vector of ray
  vec2 invDir = vec2(1.0f / rayDir.x, 1.0f / rayDir.y);

  // lb is the corner of AABB with minimal coordinates - left bottom, rt is maximal corner
  // r.org is origin of ray
  float t1 = (boxMin.x - rayStart.x) * invDir.x;
  float t2 = (boxMax.x - rayStart.x) * invDir.x;
  float t3 = (boxMin.y - rayStart.y) * invDir.y;
  float t4 = (boxMax.y - rayStart.y) * invDir.y;

  paramMin = max(min(t1, t2), min(t3, t4));
  paramMax = min(max(t1, t2), max(t3, t4));

  return paramMin < paramMax;
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

void main() 
{
  vec2 centerPixelCoord = gl_FragCoord.xy;
  //vec2 centerScreenCoord = (ivec2(centerPixelCoord) * downsamplingScale + downsamplingOffset + vec2(0.5)) / viewportSize;
  vec2 centerScreenCoord = centerPixelCoord / viewportSize.xy;
  vec4 centerNormalSample = textureLod(normalSampler, centerScreenCoord, 0.0);
  vec4 centerDepthSample = textureLod(depthStencilSampler, centerScreenCoord, 0.0);
  vec4 centerLightSample = textureLod(blurredDirectLightSampler, centerScreenCoord, 0.0);

  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  mat4 invViewMatrix = inverse(viewMatrix);
  mat4 invProjMatrix = inverse(projMatrix);

  vec3 camWorldPos = (invViewMatrix * vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
  
  vec3 centerWorldPos = Unproject(vec3(centerScreenCoord, centerDepthSample.r), invViewProjMatrix);
  vec3 centerViewPos = Unproject(vec3(centerScreenCoord, centerDepthSample.r), invProjMatrix);

  vec3 centerWorldNorm = centerNormalSample.xyz;
  /*if(dot(centerWorldNorm, centerWorldPos - camWorldPos) > 0.0)
    centerWorldNorm = -centerWorldNorm;*/
  
  //vec3 centerBlurredViewPos = normalize(Unproject(vec3(centerScreenCoord, 1.0f), inverse(projMatrix))) * textureLod(blurredDepthMomentsSampler, centerScreenCoord, 0.0).r;
  //vec3 centerWorldPos = (inverse(viewMatrix) * vec4(centerBlurredViewPos, 1.0f)).xyz;
  //vec3 centerWorldPos = camWorldPos + normalize(Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix) - camWorldPos) * textureLod(blurredDepthMomentsSampler, centerScreenCoord, 0.0).r;
  
  const int interleavedPatternSize = 4;
  int offsets2[16] = {
    4,  8,  2,  9,
    15,  0,  12,  5,
    1,  3,  10, 11,
    6, 13, 7, 14
  };
  int offsets[16] = {
    0,  4,  8,  12,
    5,  9,  13,  2,
    8,  11,  15, 3,
    14, 6, 10, 7
  };
  vec3 ambientLight = vec3(0.01f);
  ivec2 patternIndex = ivec2(centerPixelCoord) % interleavedPatternSize;
  /*int angOffsetInt = offsets[patternIndex.x + patternIndex.y * 4];
  int linOffsetInt = (angOffsetInt % 4) * 4 + angOffsetInt / 4;
  float angOffset = float(angOffsetInt) / 16.0f;
  float linOffset = float(linOffsetInt) / 16.0f;*/
  
  int index = patternIndex.x + patternIndex.y * 4;
  vec2 distribution = HammersleyNorm(index, 16);
  float angOffset = distribution.x;
  float linOffset = distribution.y;
  
  const int dirsCount = 4;
  const float nearStepSize = viewportSize.x / 1000.0f;
  
  const float pi = 3.1415f;
  float pixelAngOffset = 2.0 * pi / dirsCount * angOffset;
  float offsetEps = 0.01;
  
  vec3 sumLight = vec3(0.0f);
  for(int dirIndex = 0; dirIndex < dirsCount; dirIndex++)
  {
    float dirPixelOffset = linOffset;
    float screenAng = pixelAngOffset + 2.0 * pi / float(dirsCount) * float(dirIndex);
    vec2 screenPixelDir = vec2(cos(screenAng), sin(screenAng));

    float eps = 10e-1;
    vec3 offsetWorldPos = Unproject(vec3((centerPixelCoord + screenPixelDir * eps) / viewportSize.xy, centerDepthSample.r), invViewProjMatrix);
    vec3 eyeWorldDir = normalize(camWorldPos - centerWorldPos);
    vec3 eyeWorldTangent = normalize(normalize(offsetWorldPos - camWorldPos) - normalize(centerWorldPos - camWorldPos));
    /*vec3 eyeWorldDir = (invViewMatrix * vec4(0.0f, 0.0f, -1.0f, 0.0f)).xyz;
    if(dot(eyeWorldDir, centerWorldNorm) < 0.0f)
      eyeWorldDir *= -1.0f;
    vec3 eyeWorldTangent = normalize(offsetWorldPos - centerWorldPos);*/
    
    float maxHorizonAngle = -1e5;


    {
      vec3 dirNormalPoint = cross(-cross(eyeWorldTangent, eyeWorldDir), centerWorldNorm);
      vec2 projectedDirNormal = vec2(dot(dirNormalPoint, eyeWorldDir), dot(dirNormalPoint, eyeWorldTangent));
      /*if(projectedDirNormal.x < 0.0)
      projectedDirNormal = -projectedDirNormal;*/

      maxHorizonAngle = atan(projectedDirNormal.y, projectedDirNormal.x); //angle = 0 at view angle, not at horizon
    }
    
    float tmin, tmax;
    BoxRayCast(centerPixelCoord, screenPixelDir, vec2(0.0, 0.0), viewportSize.xy, tmin, tmax);
    //vec2 endPixelCoord = centerPixelCoord + screenPixelDir * tmax;
    float totalPixelPath = abs(tmax);
    
    vec3 dirLight = vec3(0.0f);
    {
       //dirLight = ambientLight * ComputeHorizonContribution(eyeWorldDir, eyeWorldTangent, centerWorldNorm, 0.0, maxHorizonAngle);
       dirLight = ambientLight * ComputeHorizonContribution(eyeWorldDir, eyeWorldTangent, centerWorldNorm, 0.0, maxHorizonAngle);
    }
    
    int iterationsCount = int(log(totalPixelPath / nearStepSize) / log(2.0f * pi / dirsCount + 1)) + 1;
    
    for(int offset = 0; offset < iterationsCount; offset++)
    {
      float pixelOffset = 0;
      pixelOffset = nearStepSize * pow(2.0f * pi / dirsCount + 1, offset + dirPixelOffset) + 1 - nearStepSize;
      vec2 samplePixelCoord = centerPixelCoord + screenPixelDir * pixelOffset;
      vec2 sampleScreenCoord = samplePixelCoord / viewportSize.xy;
      float sideMult = 1.0f;
      {
        float width = 0.1f;
        float invWidth = 1.0f / width;
        sideMult *= saturate((1.0f - sampleScreenCoord.x) * invWidth);
        sideMult *= saturate(sampleScreenCoord.x * invWidth);
        sideMult *= saturate((1.0f - sampleScreenCoord.y) * invWidth);
        sideMult *= saturate(sampleScreenCoord.y * invWidth);
      }

      //float blurLodOffset = -3.0f; //blur radius 4 = 8 pixels blur
      float blurLodOffset = -2.0f; //blur radius 2 = 4 pixels blur
      float depthLodMult = 0.5f;
      float colorLodMult = 0.5f;
      float depthLod = log(max(0, 2.0f * pi / dirsCount * (pixelOffset - 1.0f) * depthLodMult)) / log(2.0) + blurLodOffset;
      float colorLod = log(max(0, 2.0f * pi / dirsCount * (pixelOffset - 1.0f) * colorLodMult)) / log(2.0) + blurLodOffset;

      /*vec4 depthSample = textureLod(depthStencilSampler, sampleScreenCoord.xy, 0.0f);
      vec3 sampleWorldPos = Unproject(vec3(sampleScreenCoord, depthSample.r), invViewProjMatrix);*/

      vec4 depthSample = textureLod(blurredDepthMomentsSampler, sampleScreenCoord.xy, depthLod);
      vec3 sampleWorldPos = camWorldPos + normalize(Unproject(vec3(sampleScreenCoord, 1.0f), invViewProjMatrix) - camWorldPos) * depthSample.r;



      /*vec4 mippedCenterDepthSample = textureLod(blurredDepthMomentsSampler, centerScreenCoord.xy, depthLod);
      vec3 mippedCenterWorldPos = camWorldPos + normalize(Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix) - camWorldPos) * mippedCenterDepthSample.r;

      vec3 worldDelta = sampleWorldPos - mippedCenterWorldPos;*/
      vec3 worldDelta = sampleWorldPos - centerWorldPos;
      vec2 horizonPoint = vec2(dot(eyeWorldDir, worldDelta), dot(eyeWorldTangent, worldDelta));

      float sampleHorizonAngle = atan(horizonPoint.y, horizonPoint.x);

      if(sampleHorizonAngle < maxHorizonAngle)
      {
        vec4 lightSample = textureLod(blurredDirectLightSampler, sampleScreenCoord.xy, colorLod);

        float horizonContribution = ComputeHorizonContribution(eyeWorldDir, eyeWorldTangent, centerWorldNorm, sampleHorizonAngle, maxHorizonAngle) * sideMult;
        //horizonContribution = min(horizonContribution, 0.05f);

        dirLight += lightSample.rgb * horizonContribution;
        dirLight -= ambientLight * horizonContribution;
        maxHorizonAngle = sampleHorizonAngle;
      }
    }

    
    sumLight += vec3(2.0) * dirLight / float(dirsCount); // integral ... sin * cos = pi. angle is 2*pi. => mult 2.
  }
  indirectLight = vec4(sumLight, 1.0f);
  //indirectLight = (sumLight.x > 0.9f && sumLight.x < 1.1f) ? vec4(0.0f, 1.0f, 0.0f, 1.0f) : vec4(1.0f, 0.0f, 0.0f, 1.0f);//vec4(sumLight, 1.0f);
}