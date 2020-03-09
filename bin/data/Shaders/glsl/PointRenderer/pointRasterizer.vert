#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "../Common/PointsData.decl"
layout(location = 0) in vec3 attribPosition;
layout(location = 1) in vec3 attribNormal;
layout(location = 2) in vec2 attribUv;

layout(binding = 0, set = 0) uniform PointRasterizerData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  float time;
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

float rand(vec2 co){
  return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main()
{
	vertWorldPos    = (modelMatrix * vec4(attribPosition, 1.0f)).xyz;
	vertWorldNormal = (modelMatrix * vec4(attribNormal,   0.0f)).xyz;
	gl_Position = projMatrix * viewMatrix * vec4(vertWorldPos, 1.0f);
	gl_PointSize = 1.0f;
	pointIndex = basePointIndex + gl_VertexIndex;

	//vertWorldPos += vertWorldNormal * fract(pointIndex * 0.01f) * 0.01f;
	vertWorldPos += (vec3(rand(vec2(pointIndex, 0.0f)), rand(vec2(pointIndex, 0.1f)), rand(vec2(pointIndex, 0.2f))) - vec3(0.5f)) * 1e-3f;
	/*if(pointIndex >= pointsBuf.data.length())
	{
		vertWorldPos *= 0;
		vertWorldNormal *= 0;
		gl_Position *= 0;
	}*/
	pointsBuf.data[pointIndex].worldPos = vec4(vertWorldPos, 0.0f);
	pointsBuf.data[pointIndex].worldNormal = vec4(vertWorldNormal, 0.0f);
	pointsBuf.data[pointIndex].directLight.rgb = emissiveColor.rgb;
	pointsBuf.data[pointIndex].worldRadius = attribUv.x;
	
	vertUv = attribUv;
}