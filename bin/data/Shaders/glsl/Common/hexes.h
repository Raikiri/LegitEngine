const vec2 hexRatio = vec2(1.0, sqrt(3.0));

//credits for hex tiling goes to Shane (https://www.shadertoy.com/view/Xljczw)
//center, index
vec4 GetHexGridInfo(vec2 uv)
{
  vec4 hexIndex = round(vec4(uv, uv - vec2(0.5, 1.0)) / hexRatio.xyxy);
  vec4 hexCenter = vec4(hexIndex.xy * hexRatio, (hexIndex.zw + 0.5) * hexRatio);
  vec4 offset = uv.xyxy - hexCenter;
  return dot(offset.xy, offset.xy) < dot(offset.zw, offset.zw) ? 
    vec4(hexCenter.xy, hexIndex.xy) : 
    vec4(hexCenter.zw, hexIndex.zw);
}

float GetHexSDF(in vec2 p)
{
  p = abs(p);
  return 0.5 - max(dot(p, hexRatio * 0.5), p.x);
}

//xy: node pos, z: weight
vec3 GetTriangleInterpNode(in vec2 pos, in float freq, in int nodeIndex)
{
  vec2 nodeOffsets[] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(1.0,-1.0));

  vec2 uv = pos * freq + nodeOffsets[nodeIndex] / hexRatio.xy * 0.5;
  vec4 hexInfo = GetHexGridInfo(uv);
  float dist = GetHexSDF(uv - hexInfo.xy) * 2.0;
  return vec3(hexInfo.xy / freq, dist);
}

vec3 PreserveVariance(vec3 linearColor, vec3 meanColor, float moment2)
{
  return (linearColor - meanColor) / sqrt(moment2) + meanColor;
}