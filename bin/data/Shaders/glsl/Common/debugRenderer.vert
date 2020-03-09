#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex 
{
	vec4 gl_Position;
};

layout(binding = 0, set = 0) uniform QuadData
{
  vec4 minmax;
};

layout(location = 0) out vec2 vertTexCoord;

void main()
{
  vec2 texCoords[] = vec2[](vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), vec2(1.0f, 1.0f), vec2(0.0f, 1.0f));
  vec2 screenPoints[] = vec2[](vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), vec2(1.0f, 1.0f), vec2(0.0f, 1.0f));
	gl_Position.xy = (minmax.xy + screenPoints[gl_VertexIndex] * (minmax.zw - minmax.xy)) * 2.0f - vec2(1.0f);
  gl_Position.z = 0.5f;
  gl_Position.w = 1.0f;
  vertTexCoord = texCoords[gl_VertexIndex];
}