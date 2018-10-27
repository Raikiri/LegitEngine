#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;


out gl_PerVertex 
{
	vec4 gl_Position;
};

layout(location = 0) out vec4 vertColor;
layout(location = 1) out vec2 vertUv;

void main()
{
	/*vec2 positions[3] = vec2[]
	(
		vec2(0.0, -0.5),
		vec2(0.5, 0.5),
		vec2(-0.5, 0.5)
	);

	vec4 colors[3] = vec4[]
	(
		vec4(1.0, 0.0, 0.0, 1.0),
		vec4(0.0, 1.0, 0.0, 1.0),
		vec4(0.0, 0.0, 1.0, 1.0)
	);

	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	vertColor = colors[gl_VertexIndex];*/
	gl_Position = vec4(inPosition, 1.0);
	vertColor = inColor;
	vertUv = inUv;
}