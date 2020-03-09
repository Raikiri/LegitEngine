#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform DirectLightingData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  mat4 lightViewMatrix; //world -> camera
  mat4 lightProjMatrix; //camera -> ndc
  float time;
};

layout(binding = 1, set = 0) uniform sampler2D albedoSampler;
layout(binding = 2, set = 0) uniform sampler2D emissiveSampler;
layout(binding = 3, set = 0) uniform sampler2D normalSampler;
layout(binding = 4, set = 0) uniform sampler2D depthStencilSampler;
layout(binding = 5, set = 0) uniform sampler2DShadow shadowmapSampler;

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
  vec4 depthSample = texture(depthStencilSampler, fragScreenCoord);
  vec4 normalSample = texture(normalSampler, fragScreenCoord);
  vec3 worldNormal = normalSample.xyz;
  mat4 viewProjMatrix = projMatrix * viewMatrix;
  
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec3 lightPos = (inverse(lightViewMatrix) * vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
  
  vec3 worldPos = Unproject(vec3(fragScreenCoord, depthSample.r), invViewProjMatrix);
  vec3 lightVec = worldPos - lightPos.xyz;  
  float diffuse = max(0.0f, -dot(normalize(lightVec), worldNormal));// / (dot(lightVec, lightVec) + 1e-7f);

  mat4 lightViewProjMatrix = lightProjMatrix * lightViewMatrix;  
  vec3 shadowmapCoord = Project(worldPos, lightViewProjMatrix);
  vec3 lightViewPos = (lightViewMatrix * vec4(worldPos, 1.0f)).xyz;
  
  vec3 lightIntensity = vec3(5.0f/*300.0f*/);
  float radius = length(shadowmapCoord.xy - vec2(0.5f)) * 2.0f;
  float penumbra = smoothstep(0, 1, 1.0f - saturate((radius - 0.6f) / (1.0f - 0.6f))) * (lightViewPos.z > 0.0f ? 1.0f : 0.0f);
  lightIntensity *= penumbra;
  
  float bias = 2e-4f;
  shadowmapCoord.z -= bias;
  /*if(textureLod(shadowmapSampler, shadowmapCoord.xy, 0.0f).r < shadowmapCoord.z)
    lightIntensity *= 0.0f;*/
  lightIntensity *= pow(texture(shadowmapSampler, shadowmapCoord).r, 2.2f); //this somehow produces better results
  
  //outColor = vec4(fract(fragScreenCoord.xy * 10.0f), 0.0f, 1.0f);
  vec4 albedoColor = textureLod(albedoSampler, fragScreenCoord, 0.0f);
  vec4 emissiveColor = textureLod(emissiveSampler, fragScreenCoord, 0.0f);
  //vec4 albedoColor = texelFetch(albedoSampler, ivec2(gl_FragCoord.xy), 1);
  outColor = vec4(diffuse * albedoColor.rgb * lightIntensity.rgb + emissiveColor.rgb, 1.0f);//vec4(diffuse);//vec4(fract(worldPos.x));//
  //outColor = pow(outColor, vec4(2.2f));
  //outColor = outColor.r > 0.5f ? vec4(1.0f) : vec4(0.0f);
  //outColor = vec4(fract(gl_FragCoord.xy), 0.0f, 1.0f);
}