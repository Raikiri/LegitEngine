#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform ImGuiShaderData
{
	mat4 projMatrix;
};

layout(binding = 0, set = 1) uniform sampler2D tex;

/*layout(binding = 0, set = 1) uniform DrawCallData
{
	mat4 modelMatrix; //object->world
	vec4 albedoColor;
	vec4 emissiveColor;
};*/

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;


void main() 
{
	outColor = fragColor * texture(tex, fragUv.xy);
}