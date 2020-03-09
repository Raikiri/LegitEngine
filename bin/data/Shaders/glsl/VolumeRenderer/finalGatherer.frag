#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform FinalGathererData
{
  vec4 viewportSize;
  float totalWeight;
};

layout(binding = 1, set = 0) uniform sampler2D accumulatedLight;

layout(location = 0) in vec2 fragScreenCoord;

layout(location = 0) out vec4 outColor;


void main() 
{
  /*vec4 directLight = textureLod(directLightSampler, fragScreenCoord, 0);  
  vec4 albedoColor = textureLod(albedoSampler, fragScreenCoord, 0);*/  
  ivec2 viewportSize2i = ivec2(viewportSize.xy + vec2(0.5f));
  ivec2 pixelCoord = ivec2(fragScreenCoord.xy * viewportSize.xy);
  

  vec4 lightSample = texture(accumulatedLight, fragScreenCoord.xy);

  //outColor = vec4(lightSample.rgb / totalWeight, 1.0f);
  float exposure = 1.0f;
  outColor = vec4(vec3(1.0f) - exp(-lightSample.rgb / totalWeight * exposure), 1.0f);
}