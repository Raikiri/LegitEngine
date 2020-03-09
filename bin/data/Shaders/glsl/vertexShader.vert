#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 attribPosition;
layout(location = 1) in vec3 attribNormal;
layout(location = 2) in vec2 attribUv;

layout(binding = 0, set = 0) uniform FrameData
{
	float time;
};

layout(binding = 0, set = 1) uniform PassData
{
	mat4 projMatrix; //view->ndc
	mat4 viewMatrix; //world->view
};

layout(binding = 0, set = 2) uniform DrawCallData
{
	mat4 modelMatrix; //object->world
};

out gl_PerVertex 
{
	vec4 gl_Position;
};

layout(location = 0) out vec3 vertWorldPos;
layout(location = 1) out vec3 vertWorldNormal;
layout(location = 2) out vec2 vertUv;

void main()
{
	vertWorldPos    = (modelMatrix * vec4(attribPosition, 1.0f)).xyz;
	vertWorldNormal = (modelMatrix * vec4(attribNormal,   0.0f)).xyz;
	gl_Position = projMatrix * viewMatrix * vec4(vertWorldPos, 1.0f);
	vertUv = attribUv;
}