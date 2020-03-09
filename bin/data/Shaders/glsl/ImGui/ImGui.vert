#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 attribPosition;
layout(location = 1) in vec2 attribUv;
layout(location = 2) in vec4 attribColor;

layout(binding = 0, set = 0) uniform ImGuiShaderData
{
	mat4 projMatrix;
};

/*layout(binding = 0, set = 1) uniform DrawCallData
{
	mat4 modelMatrix; //object->world
	vec4 albedoColor;
	vec4 emissiveColor;
};*/

out gl_PerVertex 
{
	vec4 gl_Position;
};

layout(location = 0) out vec2 vertUv;
layout(location = 1) out vec4 vertColor;

void main()
{
	vertUv = attribUv;
	//vertColor = attribColor;
	vertColor = vec4(pow(attribColor.rgb, vec3(2.2f)), attribColor.a); //imgui colors are srgb space
	gl_Position = projMatrix * vec4(attribPosition, 0.0f, 1.0f);
}