#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform BucketSortData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  float time;
};
struct Bucket
{
  uint pointIndex;
  uint pointDataCount;
};

layout(std430, binding = 1, set = 0) buffer BucketData
{
  Bucket bucketData[];
};

#define PointIndex uint
#define null PointIndex(-1)

struct Point
{
  vec4 worldPos;
  vec4 worldNormal;
  vec4 directLight;
  vec4 indirectLight;
  PointIndex next;
  float padding[3];
};

layout(std430, binding = 2, set = 0) buffer PointData
{
  Point pointData[];
};

layout(location = 0) in vec2 fragScreenCoord;
layout(location = 0) out vec4 outColor;

vec3 Unproject(vec3 screenPos, mat4 inverseProjectionMatrix)
{
  vec4 viewPos = inverseProjectionMatrix * vec4((screenPos.xy * 2.0 - 1.0), screenPos.z, 1.0);
  viewPos /= viewPos.w;
  return viewPos.xyz;
}

vec3 Project(vec3 viewPos, mat4 projectionMatrix)
{
  vec4 normalizedDevicePos = projectionMatrix * vec4(viewPos, 1.0);
  normalizedDevicePos.xyz /= normalizedDevicePos.w;

  vec3 screenPos = vec3(normalizedDevicePos.xy * 0.5 + vec2(0.5), normalizedDevicePos.z);
  return screenPos;
}

uint GetListSize(PointIndex head)
{
  uint count = 0;
  PointIndex curr = head;
  while (curr != null)
  {
    count++;
    curr = pointData[curr].next;
  }
  return count;
}

PointIndex SortedInsert(PointIndex head, PointIndex newPoint, vec3 sortDir, inout uint err)
{
  err++;
  if (head == null || dot(pointData[head].worldPos.xyz - pointData[newPoint].worldPos.xyz, sortDir) >= 0.0f)
  {
    pointData[newPoint].next = head;
    head = newPoint;
  }
  else
  {
    PointIndex curr = head;
    for (; pointData[curr].next != null && dot(pointData[pointData[curr].next].worldPos.xyz - pointData[newPoint].worldPos.xyz, sortDir) < 0.0f && err < 100000; curr = pointData[curr].next)
    {
      err++;
    }
    pointData[newPoint].next = pointData[curr].next;
    pointData[curr].next = newPoint;
  }
  return head;
}

PointIndex InsertionSort(PointIndex head, vec3 sortDir, inout uint err)
{
  PointIndex newHead = null;
  PointIndex curr = head;
  while (curr != null && err < 100000)
  {
    PointIndex next = pointData[curr].next;
    newHead = SortedInsert(newHead, curr, sortDir, err);
    curr = next;
  }
  return newHead;
}

PointIndex BubbleSort(PointIndex head, vec3 sortDir)
{
  bool wasChanged;
  do
  {
    PointIndex curr = head;
    PointIndex prev = null;
    PointIndex next = pointData[head].next;
    wasChanged = false;
    while (next != null) 
    {
      if (dot(pointData[curr].worldPos.xyz - pointData[next].worldPos.xyz, sortDir) < 0.0f)
      {
        wasChanged = true;

        if (prev != null)
        {
          PointIndex tmp = pointData[next].next;

          pointData[prev].next = next;
          pointData[next].next = curr;
          pointData[curr].next = tmp;
        }
        else 
        {
          PointIndex tmp = pointData[next].next;

          head = next;
          pointData[next].next = curr;
          pointData[curr].next = tmp;
        }

        prev = next;
        next = pointData[curr].next;
      }
      else
      {
        prev = curr;
        curr = next;
        next = pointData[next].next;
      }
    }
  } while (wasChanged);
  return head;
}


struct MergeResult
{
  PointIndex head;
  PointIndex tail;
};

MergeResult MergeLists(PointIndex leftHead, PointIndex rightHead, vec3 sortDir)
{
  PointIndex currLeft = leftHead;
  PointIndex currRight = rightHead;
  MergeResult res;
  if (dot(pointData[currLeft].worldPos.xyz - pointData[currRight].worldPos.xyz, sortDir) < 0.0f)
  {
    res.head = currLeft;
    currLeft = pointData[currLeft].next;
  }
  else
  {
    res.head = currRight;
    currRight = pointData[currRight].next;
  }
  PointIndex currMerged = res.head;
  while (currLeft != null || currRight != null)
  {
    //itCount++; //~10000 iterations for random 1000-element array
    if (currRight == null || (currLeft != null && dot(pointData[currLeft].worldPos.xyz - pointData[currRight].worldPos.xyz, sortDir) < 0.0f))
    {
      pointData[currMerged].next = currLeft;
      currMerged = currLeft;
      currLeft = pointData[currLeft].next;
    }
    else
    {
      pointData[currMerged].next = currRight;
      currMerged = currRight;
      currRight = pointData[currRight].next;
    }
  }
  pointData[currMerged].next = null;
  res.tail = currMerged;
  return res;
}

PointIndex SeparateList(PointIndex head, uint count)
{
  PointIndex curr = head;
  for (uint i = 0; i < count - 1 && pointData[curr].next != null; i++)
    curr = pointData[curr].next;
  PointIndex nextHead = pointData[curr].next;
  pointData[curr].next = null;
  return nextHead;
}

PointIndex MergeSort(PointIndex head, vec3 sortDir)
{
  uint count = GetListSize(head);
  for (uint gap = 1; gap < count; gap *= 2)
  {
    PointIndex lastTail = null;
    PointIndex curr = head;
    while (curr != null)
    {
      PointIndex leftHead = curr;
      PointIndex rightHead = SeparateList(leftHead, gap);
      if (rightHead == null)
        break;

      PointIndex nextHead = SeparateList(rightHead, gap);

      MergeResult mergeResult = MergeLists(leftHead, rightHead, sortDir);
      if (lastTail != null)
        pointData[lastTail].next = mergeResult.head;
      pointData[mergeResult.tail].next = nextHead;
      lastTail = mergeResult.tail;
      if (curr == head)
        head = mergeResult.head;
      curr = nextHead;
    }
  }
  return head;
}

void main() 
{
  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);
  
  vec2 centerScreenCoord = gl_FragCoord.xy / viewportSize.xy;
  ivec2 size = ivec2(viewportSize.x + 0.5f, viewportSize.y + 0.5f);
  ivec2 bucketCoord = ivec2(gl_FragCoord.xy);

  vec3 rayOrigin = Unproject(vec3(centerScreenCoord, 0.0f), invViewProjMatrix);
  vec3 rayEnd = Unproject(vec3(centerScreenCoord, 1.0f), invViewProjMatrix);
  vec3 rayDir = normalize(rayEnd - rayOrigin);
  
  int index = bucketCoord.x + bucketCoord.y * size.x;
  if(bucketCoord.x > 0 && bucketCoord.y > 0 && bucketCoord.x < size.x && bucketCoord.y < size.y)
  {
    //uint prev = atomicExchange(bucketData[bucketCoord.x + bucketCoord.y * size.x].pointDataCount, uint(-1));
    //if(prev != uint(-1))
    {
      PointIndex head = bucketData[bucketCoord.x + bucketCoord.y * size.x].pointIndex;
      if(head != null)
      {
        uint err = 0;
        head = InsertionSort(head, rayDir, err);
        //head = BubbleSort(head, rayDir);
        //head = MergeSort(head, rayDir);
        bucketData[bucketCoord.x + bucketCoord.y * size.x].pointIndex = head;
        bucketData[bucketCoord.x + bucketCoord.y * size.x].pointDataCount += err;
        if(err > 210 * 10)
          bucketData[0].pointDataCount = 10000;
        //atomicMax(bucketData[0].pointDataCount, err);
      }
    }
  }
  
  outColor = vec4(0.0f);
}