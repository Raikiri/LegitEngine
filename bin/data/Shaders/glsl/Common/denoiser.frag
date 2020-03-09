#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform DenoiserData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  int radius;
};

layout(binding = 1, set = 0) uniform sampler2D noisySampler;
layout(binding = 2, set = 0) uniform sampler2D normalSampler;
layout(binding = 3, set = 0) uniform sampler2D depthStencilSampler;

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

#define argsCount 2
#define equationsCount 16

struct Equation
{
  float params[argsCount];
  vec3 color;
};
//X : eqCount x argsCount
//b = (XT X)-1 XT y

//XT X : argsCount argsCount

Equation MakeEquation(vec2 pixelCoord)
{
  vec2 screenCoord = pixelCoord / viewportSize.xy;
  vec4 normalSample = textureLod(normalSampler, screenCoord, 0.0f);
  vec4 depthSample = textureLod(depthStencilSampler, screenCoord, 0.0f);
  vec4 colorSample = textureLod(noisySampler, screenCoord, 0.0f);
  
  ivec2 iPixelCoord = ivec2(pixelCoord);
  
  ivec2 patternIndex = iPixelCoord % ivec2(4, 4);
  Equation res;
  int paramIndex = 0;
  //res.params[paramIndex++] = normalSample.x;
  //res.params[paramIndex++] = normalSample.y;
  //res.params[paramIndex++] = normalSample.z;
  res.params[paramIndex++] = depthSample.r;
  res.params[paramIndex++] = 1.0f;
  //res.params[paramIndex++] = 
  /*res.params[paramIndex++] = pixelCoord.x;
  res.params[paramIndex++] = pixelCoord.y;*/
  res.color = colorSample.rgb;
  return res;
}

void main()
{
  vec2 centerPixelCoord = gl_FragCoord.xy;
  vec2 centerScreenCoord = centerPixelCoord / viewportSize.xy;

  vec4 centerNormalSample = textureLod(normalSampler, centerScreenCoord, 0.0f);
  vec4 centerDepthSample = textureLod(depthStencilSampler, centerScreenCoord, 0.0f);
  vec3 centerNormal = centerNormalSample.rgb;
  float centerDepth = centerDepthSample.x;

  if(radius == 0)
  {
    resColor = textureLod(noisySampler, centerScreenCoord, 0.0f);
    return;
  }
  /*vec4 sumColor = vec4(0.0f);
  float sumWeight = 0.0f;
  const int radius = 2;
  for(int y = -radius; y < radius; y++)
  {
    for(int x = -radius; x < radius; x++)
    {
      vec2 pixelCoord = centerPixelCoord + vec2(x, y);
      vec2 screenCoord = pixelCoord / viewportSize.xy;
      vec4 normalSample = textureLod(normalSampler, screenCoord, 0.0f);
      vec4 depthSample = textureLod(depthStencilSampler, screenCoord, 0.0f);
      vec4 colorSample = textureLod(noisySampler, screenCoord, 0.0f);
      vec3 normal = normalSample.rgb;
      float depth = depthSample.r;
      
      float weight = 1.0f;
      //weight *= exp(-abs(depth - centerDepth) * 10.5f);
      vec3 normalDiff = normal - centerNormal;
      //weight *= exp(-dot(normalDiff, normalDiff) * 20.0f);
      sumColor += colorSample * weight;
      sumWeight += weight;
    }
  }
  resColor = vec4(sumColor.rgb / (sumWeight + 1e-7f), 1.0f);*/
  
  Equation equations[equationsCount];
  
  int i = 0;
  const int radius = 2;
  for(int y = -radius; y < radius; y++)
  {
    for(int x = -radius; x < radius; x++)
    {
      vec2 pixelCoord = centerPixelCoord + vec2(x, y);
      equations[i++] = MakeEquation(pixelCoord);
    }
  }
  
  //gramianMatrix = equations^T * equations
  
  mat4 gramianMatrix;
  for(int i = 0; i < 4; i++)
  {
    for(int j = 0; j < 4; j++)
    {
      gramianMatrix[i][j] = (i == j) ? 1.0f : 0.0f;
    }
  }
  for(int i = 0; i < argsCount; i++)
  {
    for(int j = 0; j < argsCount; j++)
    {
      float sum = 0.0f;
      for(int k = 0; k < equationsCount; k++)
      {
        sum += equations[k].params[i] * equations[k].params[j];
      }
      sum += ((i == j) ? 1e-7f : 0.0f);
      gramianMatrix[i][j] = sum;
    }
  }
  mat4 invGramianMatrix = transpose(inverse(gramianMatrix));
  Equation centerEquation = MakeEquation(centerPixelCoord);
  

  //vec4 resColor;
  resColor.a = 1.0f;
  for(int channel = 0; channel < 3; channel++)
  {
    float momentsVec[argsCount];
    for(int i = 0; i < argsCount; i++)
    {
      float moment = 0;
      for(int k = 0; k < equationsCount; k++)
      {
        moment += equations[k].params[i] * equations[k].color[channel];
      }
      momentsVec[i] = moment;
    }
    
    float channelCoeffs[argsCount];
    for(int i = 0; i < argsCount; i++)
    {
      float coeff = 0.0f;
      for(int j = 0; j < argsCount; j++)
      {
        coeff += invGramianMatrix[i][j] * momentsVec[j];
      }
      channelCoeffs[i] = coeff;
    }
    
    float channelVal = 0.0f;
    for(int i = 0; i < argsCount; i++)
    {
      channelVal += channelCoeffs[i] * centerEquation.params[i];
    }
    resColor[channel] = channelVal;
  }
}