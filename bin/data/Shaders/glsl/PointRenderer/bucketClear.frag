#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform BucketClearData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  float time;
};
struct Bucket
{
	uint pointIndex;
	uint pointsCount;
};

layout(std430, binding = 1, set = 0) buffer BucketData
{
	Bucket bucketData[];
};

layout(location = 0) in vec2 fragScreenCoord;
layout(location = 0) out vec4 outColor;

void main() 
{
	ivec2 size = ivec2(viewportSize.x + 0.5f, viewportSize.y + 0.5f);
	ivec2 bucketCoord = ivec2(gl_FragCoord.xy);

	if(bucketCoord.x >= 0 && bucketCoord.y >= 0 && bucketCoord.x < size.x && bucketCoord.y < size.y)
	{
		bucketData[bucketCoord.x + bucketCoord.y * size.x].pointIndex = uint(-1);
		bucketData[bucketCoord.x + bucketCoord.y * size.x].pointsCount = 0;
	}
	outColor = vec4(0.0f);
}