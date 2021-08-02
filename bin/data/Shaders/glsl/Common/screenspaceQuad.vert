#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex 
{
	vec4 gl_Position;
};

layout(location = 0) out vec2 vertScreenPos;

void main()
{
  vec2 screenPositions[] = vec2[](vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), vec2(1.0f, 1.0f), vec2(0.0f, 1.0f));
  vec2 ndcPositions[] = vec2[](vec2(-1.0f, -1.0f), vec2(1.0f, -1.0f), vec2(1.0f, 1.0f), vec2(-1.0f, 1.0f));
	gl_Position.xy = ndcPositions[gl_VertexIndex];
  gl_Position.z = 0.5f;
  gl_Position.w = 1.0f;
  vertScreenPos = screenPositions[gl_VertexIndex];
}