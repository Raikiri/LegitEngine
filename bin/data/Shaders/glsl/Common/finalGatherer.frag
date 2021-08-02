#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform FinalGathererData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
};

layout(binding = 1, set = 0) uniform sampler2D directLightSampler;
layout(binding = 2, set = 0) uniform sampler2D blurredDirectLightSampler;
layout(binding = 3, set = 0) uniform sampler2D albedoSampler;
layout(binding = 4, set = 0) uniform sampler2D indirectLightSampler;


layout(location = 0) in vec2 fragScreenCoord;

layout(location = 0) out vec4 outColor;


vec3 Unproject(vec3 screenPos, mat4 inverseProjectionMatrix)
{
	vec4 viewPos = inverseProjectionMatrix * vec4((screenPos.xy * 2.0 - 1.0), screenPos.z, 1.0);
	viewPos /= viewPos.w;
	return viewPos.xyz;
}

float saturate(float val)
{
  return clamp(val, 0.0f, 1.0f);
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
  vec4 directLight = textureLod(directLightSampler, fragScreenCoord, 0);
  
  vec4 albedoColor = textureLod(albedoSampler, fragScreenCoord, 0);
  vec4 indirectLight = textureLod(indirectLightSampler, fragScreenCoord, 0);
  outColor = directLight + indirectLight * albedoColor;
  
  float totalWeight = 1.0f;
  float currWeight = 1.0f;
  for(int i = 0; i < 8; i++)
  {
    currWeight *= 0.0f;
    outColor += textureLod(blurredDirectLightSampler, fragScreenCoord, i) * currWeight;
    totalWeight += currWeight;
  }
  outColor /= totalWeight;
  //outColor = vec4(fract(gl_FragCoord.xy), 0.0f, 1.0f);
}