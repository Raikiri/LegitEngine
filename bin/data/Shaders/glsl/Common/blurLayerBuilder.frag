#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform BlurLayerBuilderData
{
  ivec4 size;
  int radius;
};

layout(binding = 1, set = 0) uniform sampler2D srcSampler;

layout(location = 0) in vec2 fragScreenCoord;

layout(location = 0) out vec4 outColor;


void main() 
{
  ivec2 offsets[] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
  vec4 sum = vec4(0.0f);
  float totalWeight = 0.0f;
  for(int x = -radius; x < radius; x++)
  {
    for(int y = -radius; y < radius; y++)
    {
      sum += texelFetch(srcSampler, clamp(ivec2(gl_FragCoord.xy) + ivec2(x, y), ivec2(0), size.xy - ivec2(1)), 0);
      totalWeight += 1.0f;
    }
  }
  if(radius == 0)
    outColor = texelFetch(srcSampler, ivec2(gl_FragCoord.xy), 0);
  else
    outColor = sum / totalWeight;
  //outColor =  texelFetch(srcSampler, ivec2(gl_FragCoord.xy), 0);
}