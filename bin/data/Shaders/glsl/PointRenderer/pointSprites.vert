#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 attribPosition;
layout(location = 1) in vec3 attribNormal;
layout(location = 2) in vec2 attribUv;

layout(binding = 0, set = 0) uniform PassData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
	float pointWorldSize;
	float fovy;
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
};

layout(std430, binding = 1, set = 0) buffer PointData
{
	Point pointData[];
};


layout(binding = 0, set = 1) uniform DrawCallData
{
	mat4 modelMatrix; //object->world
	vec4 albedoColor;
	vec4 emissiveColor;
	uint basePointIndex;
};

out gl_PerVertex 
{
	vec4 gl_Position;
	float gl_PointSize;
};

layout(location = 0) out vec3 vertWorldPos;
layout(location = 1) out vec3 vertWorldNormal;
layout(location = 2) out vec2 vertUv;
layout(location = 3) out uint pointIndex;

void main()
{
	vertWorldPos    = (modelMatrix * vec4(attribPosition, 1.0f)).xyz;
	vertWorldNormal = (modelMatrix * vec4(attribNormal,   0.0f)).xyz;
	gl_Position = projMatrix * viewMatrix * vec4(vertWorldPos, 1.0f);
	vec3 viewPos = (viewMatrix * vec4(vertWorldPos.xyz, 1.0f)).xyz;
	gl_PointSize = 1.0f + pointWorldSize / length(viewPos) / fovy * viewportSize.y;
	pointIndex = basePointIndex + gl_VertexIndex;

	//vertWorldPos += vertWorldNormal * fract(pointIndex * 0.01f) * 0.01f;
	/*if(pointIndex >= pointData.length())
	{
		vertWorldPos *= 0;
		vertWorldNormal *= 0;
		gl_Position *= 0;
	}*/
	pointData[pointIndex].worldPos = vec4(vertWorldPos, 0.0f);
	pointData[pointIndex].worldNormal = vec4(vertWorldNormal, 0.0f);
	pointData[pointIndex].directLight.rgb = vec3(0.0f);//emissiveColor.rgb;
	//pointData[pointIndex].indirectLight = vec4(0.0f);
	pointData[pointIndex].next = uint(-1);
	
	vertUv = attribUv;
}