#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform ShadowmapBuilderData
{
	mat4 lightViewMatrix; //world->view
	mat4 lightProjMatrix; //view->ndc
};

layout(binding = 0, set = 1) uniform DrawCallData
{
	mat4 modelMatrix; //object->world
	vec4 albedoColor;
	vec4 emissiveColor;
};

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragUv;


void main() 
{
}