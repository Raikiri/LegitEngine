#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform PointRasterizerData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  float time;
};

struct Point
{
	vec4 worldPos;
	vec4 worldNormal;
	vec4 directLight;
	vec4 indirectLight;
	uint next;
	float padding[3];
	//vec3 padding;
};

layout(std430, binding = 1, set = 0) buffer PointData
{
	Point pointData[];
};

struct Bucket
{
	uint pointIndex;
	uint pointsCount;
};

layout(std430, binding = 2, set = 0) buffer BucketData
{
	Bucket bucketData[];
};


layout(binding = 0, set = 1) uniform DrawCallData
{
	mat4 modelMatrix; //object->world
	vec4 albedoColor;
	vec4 emissiveColor;
	int basePointIndex;
};


layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragUv;
layout(location = 3) in flat uint fragPointIndex;

layout(location = 0) out vec4 outColor;

void main() 
{
	ivec2 size = ivec2(viewportSize.x + 0.5f, viewportSize.y + 0.5f);
	ivec2 bucketCoord = ivec2(gl_FragCoord.xy);
	if(bucketCoord.x >= 0 && bucketCoord.y >= 0 && bucketCoord.x < size.x && bucketCoord.y < size.y)
	{
		uint prevHead = atomicExchange(bucketData[bucketCoord.x + bucketCoord.y * size.x].pointIndex, fragPointIndex);
		pointData[fragPointIndex].next = prevHead;
		atomicAdd(bucketData[bucketCoord.x + bucketCoord.y * size.x].pointsCount, 1);
	}
	/*pointData[fragPointIndex].next = bucketData[bucketCoord.x + bucketCoord.y * size.x].pointIndex;
	bucketData[bucketCoord.x + bucketCoord.y * size.x].pointIndex = max(1, fragPointIndex);*/
	
	outColor = vec4(fract(fragPointIndex / 100.0f));
}