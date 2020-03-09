#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragUv;

layout(location = 0) out vec4 outColor;

void main() 
{
	vec3 lightDir = vec3(0.0f, -1.0f, 0.0f);
	vec3 lightIntensity = vec3(1.0f, 1.0f, 1.0f);
	vec3 incidentLight = lightIntensity * max(0, dot(-lightDir, fragWorldNormal));
	vec3 surfaceAlbedo = vec3(1.0f, 0.5f, 0.0f);
	vec3 emittedLight = surfaceAlbedo * incidentLight;
	//outColor = vec4(fract(fragWorldPos.z), 0.0f, 0.0f, 1.0f);
	outColor = vec4(emittedLight, 1.0f);
}