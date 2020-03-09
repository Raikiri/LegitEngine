#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "../passData.decl"
#include "../../projection.decl"
#include "../bucketsData.decl"
#include "../../pointsData.decl"

out gl_PerVertex 
{
	vec4 gl_Position;
	float gl_PointSize;
};

layout(location = 0) out uint vertPointIndex;

void main()
{
	vertPointIndex = gl_VertexIndex;
	gl_Position = passDataBuf.projMatrix * passDataBuf.viewMatrix * vec4(pointsBuf.data[vertPointIndex].worldPos.xyz, 1.0f);
	gl_PointSize = 1.0f;
}