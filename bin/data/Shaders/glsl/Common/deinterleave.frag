#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform DeinterleaveData
{
  ivec4 gridSize;
  ivec4 viewportSize;
};

layout(binding = 1, set = 0) uniform sampler2D interleavedSampler;

layout (location = 0) in vec2 fragScreenCoord;
layout (location = 0) out vec4 resColor;


ivec2 InterleavePixel(ivec2 deinterleavedPixel, ivec2 viewportSize, ivec2 gridSize)
{
  ivec2 deinterleavedViewportSize = viewportSize.xy / gridSize;
  ivec2 patternIndex = deinterleavedPixel / deinterleavedViewportSize;
  return (deinterleavedPixel % deinterleavedViewportSize) * gridSize + patternIndex;
}

void main()
{
  ivec2 dstDeinterleavedPixel =  ivec2(gl_FragCoord.xy);
  ivec2 srcInterleavedPixel = InterleavePixel(dstDeinterleavedPixel, viewportSize.xy, gridSize.xy);
  resColor = texelFetch(interleavedSampler, srcInterleavedPixel, 0);
}