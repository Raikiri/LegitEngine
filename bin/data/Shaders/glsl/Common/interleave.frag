#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform InterleaveData
{
  ivec4 gridSize;
  ivec4 viewportSize;
};

layout(binding = 1, set = 0) uniform sampler2D deinterleavedSampler;

layout (location = 0) in vec2 fragScreenCoord;
layout (location = 0) out vec4 resColor;

ivec2 DeinterleavePixel(ivec2 interleavedPixel, ivec2 viewportSize, ivec2 gridSize)
{
  ivec2 deinterleavedViewportSize = viewportSize.xy / gridSize;
  ivec2 patternIndex = interleavedPixel % gridSize;
  return patternIndex * deinterleavedViewportSize + interleavedPixel / gridSize;
}

void main()
{
  ivec2 dstInterleavedPixel =  ivec2(gl_FragCoord.xy);
  ivec2 srcDeinterleavedPixel = DeinterleavePixel(dstInterleavedPixel, viewportSize.xy, gridSize.xy);
  resColor = texelFetch(deinterleavedSampler, srcDeinterleavedPixel, 0);
}