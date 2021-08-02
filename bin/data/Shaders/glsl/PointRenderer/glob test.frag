#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragScreenCoord;
layout(location = 0) out vec4 resColor;

#define MaxObjectsCount 20
struct Object
{
  uint bla1;
  uint bla2;
};

struct Objects
{
  Object data[MaxObjectsCount];
};

Objects objects;

void CompareExchange(/*inout Objects objects, */uint i, uint j)
{
  if(objects.data[i].bla1 > objects.data[j].bla1)
  {
    Object tmp = objects.data[i];
    objects.data[i] = objects.data[j];
    objects.data[j] = tmp;
  }
}
void main()
{
  resColor = vec4(1.0f, 0.5f, 0.0f, 1.0f);
  for(int i = 0; i < MaxObjectsCount; i++)
  {
    objects.data[i].bla1 = uint(gl_FragCoord.x) - i;
    objects.data[i].bla2 = uint(gl_FragCoord.y) + i;
  }
  for(int i = 0; i < MaxObjectsCount; i++)
  {
    for(int j = 0; j < MaxObjectsCount - i - 1; j++)
    {
      CompareExchange(/*objects, */j, j + 1);
    }
  }
  resColor.r += objects.data[0].bla1 * 1e-3f;
}

