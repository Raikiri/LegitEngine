#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform MipLevelBuilderData
{
  float filterType;
};

layout(binding = 1, set = 0) uniform sampler2D prevLevelSampler;


layout(location = 0) in vec2 fragScreenCoord;

layout(location = 0) out vec4 outColor;


void main() 
{
  ivec2 offsets[] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
  vec4 sum = vec4(0.0f);
  
  if(filterType < 0.5f)
  {
    for(int i = 0; i < 4; i++)
    {
      sum += texelFetch(prevLevelSampler, ivec2(gl_FragCoord.xy) * 2 + offsets[i], 0);
    }
    outColor = sum / 4.0f;
  }else
  {
    float minDepth = 1e5f;
    float maxDepth = -1e5f;
    float totalMass = 0.0f;
    for(int i = 0; i < 4; i++)
    {
      vec4 currSample = texelFetch(prevLevelSampler, ivec2(gl_FragCoord.xy) * 2 + offsets[i], 0);
      minDepth = min(minDepth, currSample.x);
      maxDepth = max(maxDepth, currSample.y);
      totalMass += (currSample.y - currSample.x) * currSample.z;
    }
    outColor = vec4(minDepth, maxDepth, totalMass / (maxDepth - minDepth) / 4.0f, 0.0f);
  }
}