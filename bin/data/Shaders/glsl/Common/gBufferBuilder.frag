#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform GBufferBuilderData
{
	mat4 viewMatrix; //world->view
	mat4 projMatrix; //view->ndc
	float time;
	float bla;
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

layout(location = 0) out vec4 outAlbedoColor;
layout(location = 1) out vec4 outEmissiveColor;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outDepth;

void main() 
{
	vec3 cameraPos = (inverse(viewMatrix) * vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
	float deltaLen = length(fragWorldPos - cameraPos);

	//outAlbedoColor = vec4(fract(fragWorldPos.x));
	outAlbedoColor = pow(albedoColor, vec4(2.2f));
	outNormal = vec4(fragWorldNormal, 1.0f);
	outEmissiveColor = pow(emissiveColor, vec4(2.2f));
	outDepth = vec4(deltaLen, deltaLen * deltaLen, 0.0f, 1.0f);
}