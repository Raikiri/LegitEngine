#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform VolumeRendererData
{
  mat4 viewMatrix; //world -> camera
  mat4 projMatrix; //camera -> ndc
  vec4 viewportSize;
  vec4 volumeAABBMin;
  vec4 volumeAABBMax;
  float time;
  float totalWeight;
  float resetBuffer;
};

layout(binding = 1, set = 0) uniform samplerCube specularCubemap;
layout(binding = 2, set = 0) uniform sampler3D volumeSampler;
layout(binding = 3, set = 0) uniform sampler3D preintReflectSampler;
layout(binding = 4, set = 0) uniform sampler3D preintScatterSampler;
//layout(binding = 2, set = 0) uniform sampler2D albedoSampler;

layout(location = 0) in vec2 fragScreenCoord;

layout(location = 0) out vec4 outColor;

vec3 hash33( vec3 p )
{
  p = vec3(
    dot(p, vec3(127.1,311.7, 74.7)),
    dot(p, vec3(269.5,183.3,246.1)),
    dot(p, vec3(113.5,271.9,124.6)));

  return fract(sin(p)*43758.5453123);
}
float saturate(float val)
{
  return clamp(val, 0.0f, 1.0f);
}
vec4 LoadSample(sampler3D volumeSampler, vec3 pos)
{
  return texture(volumeSampler, pos);
}

#define pi 3.141592f
vec4 SampleVolumeField(sampler3D volumeSampler, vec3 pos)
{
  /*{
    vec3 diff = pos - vec3(0.5f);
    return vec4((0.5f - length(diff)) * 2.0f * 2.5f, normalize(diff));
  }*/
  
  float eps = 1e-3f;
  
  pos.z = 1.0f - pos.z;
  mat3 r = mat3(
    vec3(1.0f, 0.0f, 0.0f),
    vec3(0.0f, 0.0f, 1.0f),
    vec3(0.0f, 1.0f, 0.0f));
  pos = r * pos;
  vec4 centerSample = LoadSample(volumeSampler, pos);
  vec3 gradient = transpose(r) * (centerSample.yzw - vec3(0.5f));
  gradient.z *= -1.0f;

  /*vec4 xOffsetSample = LoadSample(volumeSampler, pos + vec3(eps, 0.0f, 0.0f));
  vec4 yOffsetSample = LoadSample(volumeSampler, pos + vec3(0.0f, eps, 0.0f));
  vec4 zOffsetSample = LoadSample(volumeSampler, pos + vec3(0.0f, 0.0f, eps));
  vec3 gradient = vec3(
    xOffsetSample.x - centerSample.x,
    yOffsetSample.x - centerSample.x,
    zOffsetSample.x - centerSample.x) / eps;*/
  /*vec4 xOffsetSamples[2] = {texture(volumeSampler, pos + vec3(-eps, 0.0f, 0.0f)), texture(volumeSampler, pos + vec3(eps, 0.0f, 0.0f))};
  vec4 yOffsetSamples[2] = {texture(volumeSampler, pos + vec3(0.0f, -eps, 0.0f)), texture(volumeSampler, pos + vec3(0.0f, eps, 0.0f))};
  vec4 zOffsetSamples[2] = {texture(volumeSampler, pos + vec3(0.0f, 0.0f, -eps)), texture(volumeSampler, pos + vec3(0.0f, 0.0f, eps))};
  vec3 gradient = vec3(
    xOffsetSamples[1].x - xOffsetSamples[0].x,
    yOffsetSamples[1].x - yOffsetSamples[0].x,
    zOffsetSamples[1].x - zOffsetSamples[0].x) / (2.0f * eps);*/
  float minField = 0.013f;
  float maxField = 0.02f;
  return vec4( saturate((centerSample.x - minField) / (maxField- minField)), gradient);
  //return vec4( pow(centerSample.x, 0.0163f), gradient);
}


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

struct AABB3f
{
  vec3 minPoint;
  vec3 maxPoint;
};

struct Ray
{
  vec3 origin;
  vec3 dir;
};

bool RayAABBIntersect(AABB3f aabb, Ray ray, out float tmin, out float tmax)
{
  vec3 t1 = (aabb.minPoint - ray.origin) / ray.dir;
  vec3 t2 = (aabb.maxPoint - ray.origin) / ray.dir;

  tmin = max(min(t1.x, t2.x), max(min(t1.y, t2.y), min(t1.z, t2.z)));
  tmax = min(max(t1.x, t2.x), min(max(t1.y, t2.y), max(t1.z, t2.z)));
  return tmax >= tmin;
}

vec3 GetRandomSpherePoint(vec3 seed)
{
  vec3 point;
  do
  {
    point = hash33(seed) * 2.0 - 1.0;
    seed += vec3(10.0, -5.0, -0.42f);
  }while(dot(point, point) > 1.0);

  return normalize(point);
}

vec3 GetRandomHemispherePoint3(vec3 dir, vec3 seed)
{
  vec3 xVector = cross(dir, vec3(1.0, 0.0, 0.0));
  if(dot(xVector, xVector) < 1e-2)
    xVector = cross(dir, vec3(0.0, 1.0, 0.0));
  xVector = normalize(xVector);
  vec3 yVector = cross(dir, xVector);

  vec3 hash = hash33(seed);
  float r = sqrt(1.0 - hash.x * hash.x);
  float phi = 2.0 * pi * hash.y;

  return xVector * r * cos(phi) + yVector * r * sin(phi) + dir * hash.x;
}

/*vec3 GetRandomHemispherePointCosineSampled2(vec3 dir, vec2 seed)
{
  float pi = 3.141592;

  vec3 xVector = cross(dir, vec3(1.0, 0.0, 0.0));
  if(dot(xVector, xVector) < 1e-2)
    xVector = cross(dir, vec3(0.0, 1.0, 0.0));
  xVector = normalize(xVector);
  vec3 yVector = cross(dir, xVector);

  vec2 randVec = vec2(rand(seed + vec2(0.0, 0.0)), rand(seed + vec2(0.0, 1.0)));
  float r = sqrt(randVec.x);
  float phi = 2.0 * pi * randVec.y;

  return xVector * r * cos(phi) + yVector * r * sin(phi) + dir * sqrt(1.0 - randVec.x);
}*/



struct SurfaceData
{
  vec3 diffuseColor;
  float density;
  float refractionIndex;
  float roughness;
  float metalness;
};

SurfaceData TransferFunction(float field)
{
  //vec3 blue = vec3(0.0, 0.5, 0.75);
  //vec3 orange = vec3(0.8f, 0.5f, 0.1f);
  //float field = pow(volumeSample, 0.1f);
  vec3 blue = vec3(0.0, 0.5, 0.75);
  vec3 orange = vec3(0.8f, 0.4f, 0.1f);
  vec3 red =  vec3(pow(0.8f, 2.2f), pow(0.25f, 2.2f), pow(0.1f, 2.2f));

  SurfaceData resData;
  float boneRatio = saturate((field - 0.95) * 1000000.0f);
  float glazeRatio = saturate((field - 0.5) * 1000.0f + 0.0f);
  float veinRatio = saturate((field - 0.58f) * 1.0f) * (1.0f - boneRatio);
  float fleshRatio = saturate((field - 0.3f) * 1.0f) * (1.0f - veinRatio);
  float filmRatio = max(0.0f, 1.0f - abs(field - 0.5f) * 1000.0f);
  //float skinRatio = saturate((field - 0.005f) * 50.0f);
  float skinRatio = saturate((field - 0.0f) * 1000.0f) * 0.0f;
  float skinSmoothRatio = saturate((field - 0.014f) * 1000.0f - 3.0f);
  float spaceRatio = 1.0f;


  float filmDensity = 50000.0f * filmRatio * 0.0f;
  float boneDensity = 200.0f * boneRatio;
  float veinDensity = 1000.0f * veinRatio * 0.0f;
  float fleshDensity = 300.0f * fleshRatio;
  float skinDensity = 0.0f * skinRatio;
  float spaceDensity = 0.0f * spaceRatio;

  resData.density = (boneDensity + veinDensity + fleshDensity + skinDensity + spaceDensity + filmDensity);
  resData.diffuseColor = (vec3(0.95f, 0.92f, 0.85f) * boneDensity + orange * veinDensity + blue * fleshDensity + red * filmDensity) / (resData.density + 1e-9f);// ;
                                                                                                                                                                          //resData.density += saturate((volumeSample - 0.015f) * 100.0f) * 30.0f;
  resData.refractionIndex = 1.0f;
  resData.refractionIndex += (fleshRatio > 0.0f ? 1.0f : 0.0f) * 0.33f * 0.0f + (skinRatio > 0.0f ? 1.0f : 0.0f) * 0.33f + (veinRatio > 0.0f ? 1.0f : 0.0f) * 0.33f * 0.0f + (boneRatio > 0.0f ? 1.0f : 0.0f) * 0.52f * 0.0f;
  //resData.refractionIndex += saturate(-(volumeSample.x - 0.015f) * 100.0f) * 0.33f + 1.0f;
  resData.roughness = 0.0f;//0.5f * (1.0f - glazeRatio) * 0.05f;
  resData.metalness = 0.0f;//glazeRatio > 0.5f ? 1.0f : 0.0f;
  return resData;
}

/*SurfaceData TransferDerivatives(float volumeSample)
{
  float eps = 1e-3f;
  SurfaceData centerData = TransferFunction(volumeSample);
  SurfaceData offsetData = TransferFunction(volumeSample + eps);
  SurfaceData derivatives;
  derivatives.diffuseColor = (offsetData.diffuseColor - centerData.diffuseColor) / eps;
  derivatives.density = (offsetData.density - centerData.density) / eps;
  return derivatives;
}*/

//https://hal.archives-ouvertes.fr/hal-01711532/document
//Rendering Rough Opaque Materials with Interfaced Lambertian Microfacets Daniel Meneveaux, Benjamin Bringier, Emmanuelle Tauzia, M Ribardière, Lionel Simonot

mat3 MakeTbnBasis(vec3 normal)
{
  vec3 genVec = abs(normal.z) < 0.5 ? vec3(0,0,1) : vec3(1,0,0);
  
  mat3 tbnBasis;
  tbnBasis[0] = normalize( cross( genVec, normal ) );
  tbnBasis[1] = cross( normal, tbnBasis[0] );
  tbnBasis[2] = normal;
  return tbnBasis;
}
vec3 ImportanceSampleGGX( vec2 uniformSeed, float roughness, mat3 tbnBasis ) 
{
  float alpha = roughness * roughness;
  float phi = 2.0f * pi * uniformSeed.x;
  float cosTheta = sqrt( (1 - uniformSeed.y) / ( 1 + (alpha * alpha - 1) * uniformSeed.y ) );
  float sinTheta = sqrt( 1 - cosTheta * cosTheta );
  vec3 localH;
  localH.x = sinTheta * cos( phi );
  localH.y = sinTheta * sin( phi );
  localH.z = cosTheta;
  
  return tbnBasis * localH;
  //pdf of reflected vector = D * NoH / (4 * VoH) 
}


float D_GGXDistribution(float NoH, float roughness)
{
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float d = NoH * NoH * (alpha2 - 1.0f) + 1.0f;
  return alpha2 / (pi * d * d);
}

float G1_Smith(float NoV, float roughness)
{
  float alpha = roughness * roughness;
  float k = alpha / 2.0f;
  return NoV / (NoV * (1.0f - k) + k);
}

float Fresnel(float VoH)
{
  return 1.0f - pow(saturate(VoH), 5.0f);
}

float FresnelFull(float VoH, float n1, float n2)
{
  /*if(n1 < 1e-3f) return 0.0f;
  float cosi = VoH;
  float sini = sqrt(1.0f - cosi * cosi);
  float angi = asin(sini);
  float sint = n1 * sini / (n2 + 1e-7f);
  float cost = sqrt(1.0f - sint * sint);
  float angt = asin(sint);
  float Rperp = pow(sin(angi - angt) / sin(angi + angt), 2.0f);
  float Rpar = pow(tan(angi - angt) / tan(angi + angt), 2.0f);
  return (Rperp + Rpar) * 0.5f; */
  
  //https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
  float c = VoH;
  float eta = n2 / n1;
  float g2 = eta * eta - 1.0f + c * c;
  if(g2 < 0.0f)
    return 1.0f;
  
  float g = sqrt(g2);
  float a = 0.5f * (g - c) * (g - c) / ((g + c) * (g + c));
  float b1 = c * (g + c) - 1.0f;
  float b2 = c * (g - c) + 1.0f;
  return a * (1.0f + b1 * b1 / (b2 * b2));
  
  /*float f0 = pow(abs((n1 - n2) / (n1 + n2)), 2.0f);
  return f0 + (1.0f - f0) * Fresnel(VoH);*/
}

float sgn(float v)
{
  return v > 0.0f ? 1.0f : -1.0f;
}
vec3 GetRefractedDir(vec3 V, vec3 N, vec3 H, float refractionRatio)
{
  /*float c = dot(V, H);
  float eta = n1 / n2;
  float m = eta * c - sgn(dot(V, H)) * sqrt(1.0f + eta * (c * c - 1.0f));
  return H * m - eta * V;*/
  //V *= -1;
  /*if(dot(V, H) < 0)
    H *= -1.0f;*/
  float r = refractionRatio;
  float c = -dot(V, H);
  
  float tmp = 1.0 - r * r * (1.0 - c * c);
  if(abs(c) < 1e-3f)
    return V;

  if(tmp < 1e-3f)
    return V;

  return (r * V + (r * c + sgn(dot(V, H)) * sqrt(tmp)) * H);
}

float PhaseFunc(float cosAng, float g)
{
  return (1.0f - g * g) / pow(1.0f + g * g - 2.0f * g * cosAng, 1.5f);
}

Ray SphereInternalBounce(Ray ray, float radius)
{
  float travelDist = 2.0f * radius / dot(ray.dir, -normalize(ray.origin));
  Ray reflectedRay;
  reflectedRay.origin = ray.origin + ray.dir * travelDist;
  vec3 surfaceNorm = normalize(reflectedRay.origin);
  reflectedRay.dir = ray.dir - 2.0f * dot(ray.dir, surfaceNorm) * surfaceNorm;
  return reflectedRay;
}

vec3 GetSphereScatter(float n, Ray ray, float radius, vec3 seed)
{
  Ray currRay = ray;
  vec3 surfaceNorm = normalize(currRay.origin);
  float VoH = abs(dot(surfaceNorm, currRay.dir));
  float fresnel = FresnelFull(VoH, 1.0f, n);
  
  vec3 reflectedDir = currRay.dir - 2.0f * surfaceNorm * dot(surfaceNorm, currRay.dir);
  
  vec3 hash = hash33(ray.origin + seed * 2.0f);
  /*if(hash.z < fresnel)
    return reflectedDir;*/
  
  currRay.dir = GetRefractedDir(currRay.dir, surfaceNorm, surfaceNorm, 1.0f / n);
  const int count = 3;
  for(int i = 0; i < count; i++)
  {
    Ray newRay = SphereInternalBounce(currRay, 1.0f);
    surfaceNorm = normalize(newRay.origin);
    VoH = abs(dot(surfaceNorm, newRay.dir));
    fresnel = FresnelFull(VoH, n, 1.0f);
    hash = hash33(ray.origin + seed * 2.0f + vec3(0.0f, i + 0.2f, 0.0f));
    if(hash.x > fresnel || (i == count - 1))
      return  GetRefractedDir(currRay.dir, surfaceNorm, surfaceNorm, n);

    currRay = newRay;
  }
  return currRay.dir;
}
vec3 LocalRainScatter(float n, vec3 seed)
{
  vec3 hash = hash33(seed);
  
  float ang = hash.x * 2.0f * pi;
  float r = sqrt(hash.y);
  
  Ray currRay;
  currRay.origin.xy = vec2(cos(ang), sin(ang)) * r;
  currRay.origin.z = -sqrt(1.0f - max(0.0f, dot(currRay.origin.xy, currRay.origin.xy)));
  
  currRay.dir = vec3(0.0f, 0.0f, 1.0f);

  return GetSphereScatter(n, currRay, 1.0f, seed);
}
vec3 RainScatter(vec3 dir, float n, vec3 seed)
{
  mat3 basis = MakeTbnBasis(dir);
  vec3 localDir = LocalRainScatter(n, seed);
  return basis * localDir;
}

vec3 GetWavelengthRGB(float normalizedWavelength)
{
	float wavelength = normalizedWavelength * (781.0 - 380.0) + 380.0;
	float red, green, blue, factor;
	red = 0.0;
	green = 0.0;
	blue = 0.0;
    if((wavelength >= 380.0) && (wavelength < 440.0))
    {
        red = -(wavelength - 440.0) / (440.0- 380.0);
        green = 0.0;
        blue = 1.0;
    }else if((wavelength >= 440.0) && (wavelength<490.0)){
        red = 0.0;
        green = (wavelength - 440.0) / (490.0 - 440.0);
        blue = 1.0;
    }else if((wavelength >= 490.0) && (wavelength<510.0)){
        red = 0.0;
        green = 1.0;
        blue = -(wavelength - 510.0) / (510.0 - 490.0);
    }else if((wavelength >= 510.0) && (wavelength<580.0)){
        red = (wavelength - 510.0) / (580.0 - 510.0);
        green = 1.0;
        blue = 0.0;
    }else if((wavelength >= 580.0) && (wavelength<645.0)){
        red = 1.0;
        green = -(wavelength - 645.0) / (645.0 - 580.0);
        blue = 0.0;
    }else if((wavelength >= 645.0) && (wavelength<781.0)){
        red = 1.0;
        green = 0.0;
        blue = 0.0;
    }else{
        red = 0.0;
        green = 0.0;
        blue = 0.0;
    };
    // Let the intensity fall off near the vision limits
    if((wavelength >= 380.0) && (wavelength<420.0))
    {
        factor = 0.3 + 0.7*(wavelength - 380.0) / (420.0 - 380.0);
    }else
    if((wavelength >= 420.0) && (wavelength<701.0))
    {
        factor = 1.0;
    }else
    if((wavelength >= 701.0) && (wavelength<781.0))
    {
        factor = 0.3 + 0.7*(780.0 - wavelength) / (780.0 - 700.0);
    }else
    {
        factor = 0.0;
    }
    vec3 norm = pow(vec3(147.0f, 119.0f, 99.0f) / 255.0f, vec3(2.2f)) * 2.0f;
    return vec3(red, green, blue) * factor / norm;
}


vec3 CastRay(vec3 origin, vec3 dir, vec3 raySeed)
{

  
  /*float normWavelength = hash33(raySeed + vec3(0.0f, 0.0f, 1.0f)).x;
  float dispersionPow = 1.0f - 0.05f * normWavelength;
  vec3 dispersedColor = GetWavelengthRGB(normWavelength);*/
  float dispersionPow = 1.0f;
  vec3 dispersedColor = vec3(1.0f);
  /*{
    //vec3 newDir = RainScatter(dir, 1.33 - 0.1f * normWavelength, -raySeed);
    vec3 newDir = RainScatter(dir, pow(1.33f, dispersionPow), -raySeed);
    vec4 cubemapSample = vec4(pow(max(0.0f, dot(newDir, vec3(1.0f, 0.0f, 0.0f))), 2500.0f)) * 300.0f;
    
    return cubemapSample.rgb * dispersedColor;
  }*/

  
  AABB3f volumeAABB;
  volumeAABB.minPoint = volumeAABBMin.xyz;
  volumeAABB.maxPoint = volumeAABBMax.xyz;
  
  Ray viewRay;
  viewRay.origin = origin;
  viewRay.dir = dir;

  
  vec3 transparency = vec3(1.0f);
  vec3 emission = vec3(0.0f);
  
  vec3 currDir = viewRay.dir;
  
  vec3 diffuseColor = vec3(0.8f, 0.5f, 0.1f);

  int bouncesCount = 0;
  float tmin = 0;
  float tmax = 0;
  vec3 debug = vec3(0.0f, 1.0f, 0.0f);
  if(RayAABBIntersect(volumeAABB, viewRay, tmin, tmax) && tmax > 0.0f)
  {
    tmin = max(0, tmin);
    vec3 currPos = viewRay.origin + viewRay.dir * tmin;
    
    bool done = false;
    float step = 1e-2f;//
    currPos += currDir * step * hash33(raySeed).y;
    SurfaceData prevSurfaceData;
    float prevDensity = 0;
    for(int i = 0; (i < 1500) && !done; i++)
    {
      vec3 texPos = (currPos - volumeAABB.minPoint) / (volumeAABB.maxPoint - volumeAABB.minPoint);
      vec4 volumeSample = SampleVolumeField(volumeSampler, texPos);
      
      float density = volumeSample.x;
      vec3 gradient = volumeSample.yzw;
      
      SurfaceData surfaceData = TransferFunction(volumeSample.x);
      if(i == 0)
      {
        prevSurfaceData = surfaceData;
        prevDensity = volumeSample.x;
      }

      vec3 nextTexPos = (currPos + currDir * step - volumeAABB.minPoint) / (volumeAABB.maxPoint - volumeAABB.minPoint);
      vec4 nextVolumeSample = SampleVolumeField(volumeSampler, nextTexPos);
      
      float nextDensity = nextVolumeSample.x;
      
      SurfaceData nextSurfaceData = TransferFunction(nextDensity);
        
      vec3 normal = (gradient) / (length(gradient) + 1e-7f);
      
      /*if(dot(normal, currDir) > 0.0f)
        normal *= -1.0f;*/
        
      mat3 tbnBasis = MakeTbnBasis(normal);

      vec3 hash = hash33(currPos + raySeed);
      //hash = hash33(vec3(int(hash.x * 5)));

      //vec3 hash = hash33(currPos + raySeed);

      vec3 V = -currDir;
      float VoN = abs(dot(V, normal)) + 1e-7f;
      vec3 H = ImportanceSampleGGX( hash.xy, surfaceData.roughness, tbnBasis );
      float VoH = abs(dot(V, H)) + 1e-7f;
      vec3 L = 2.0f * dot(V, H) * H - V;
      float HoN = abs(dot(H, normal)) + 1e-7f;


      float sinAlpha = sqrt(1.0f - VoH * VoH);
      vec4 preintScatterSample = texture(preintScatterSampler, vec3(density, nextDensity, sinAlpha), 0);
      vec4 preintReflectSample = texture(preintReflectSampler, vec3(density, nextDensity, sinAlpha), 0);
      /*vec4 preintScatterSample = texture(preintScatterSampler, vec3(prevDensity, density, sinAlpha), 0);
      vec4 preintReflectSample = texture(preintReflectSampler, vec3(prevDensity, density, sinAlpha), 0);*/
      /*vec4 preintScatterSample = texture(preintScatterSampler, vec3(density, nextDensity, VoH), 0);
      vec4 preintReflectSample = texture(preintReflectSampler, vec3(density, nextDensity, VoH), 0);*/
      /*vec4 preintScatterSample = texture(preintScatterSampler, vec3(prevDensity, density, VoH), 0);
      vec4 preintReflectSample = texture(preintReflectSampler, vec3(prevDensity, density, VoH), 0);*/

      
      /*vec4 preintScatterSample = texture(preintScatterSampler, vec3(prevDensity, density, VoH), 0);
      vec4 preintReflectSample = texture(preintReflectSampler, vec3(prevDensity, density, VoH), 0);*/

      float fresnel = FresnelFull(VoH, surfaceData.refractionIndex, nextSurfaceData.refractionIndex);
      float roughness = surfaceData.roughness;
      float scatterProb = (1.0f - exp(-surfaceData.density * step)) * (1.0f - fresnel);
      vec3 scatterColor = surfaceData.diffuseColor;
      float refractionRatio = pow(prevSurfaceData.refractionIndex / surfaceData.refractionIndex, dispersionPow);

      /*float fresnel = preintReflectSample.a;
      float roughness = preintReflectSample.r;
      float scatterProb = preintScatterSample.a * 0;
      vec3 scatterColor = preintScatterSample.rgb;
      float refractionRatio = pow(preintReflectSample.g, dispersionPow);*/


      bool reflected = false;


        
      //if(prevSurfaceData.metalness < 0.5f && surfaceData.metalness > 0.5f)
        //fresnel = 1.0f;
        

      float weight = 1.0f;//VoH * G / (VoN * HoN + 1e-7f);
      //weight*= (1.0f - fresnel);
      if(hash.z + 1e-7f < fresnel) //reflection
      //if(fresnel > 1e-7f) //reflection
      {

        float OoN = abs(dot(L, normal));
        float G = G1_Smith(VoN, surfaceData.roughness) * G1_Smith(OoN, surfaceData.roughness);

        weight = VoH * G / (VoN * HoN);// / reflectFull * fresnelFull;

        currDir = L;
        currPos += currDir * step;
      }else //scatter
      if(hash.z < scatterProb + fresnel)
      {
        vec3 newDir = GetRandomSpherePoint(currPos * 2.0f + raySeed);
        
        vec3 g = vec3(0.0f, 0.0f, 0.0f);
        //vec3 g = vec3(0.3f, 0.5f, 0.7f);
        float cosAng = dot(newDir, currDir);
        transparency *= scatterColor * vec3(PhaseFunc(cosAng, g.x), PhaseFunc(cosAng, g.y), PhaseFunc(cosAng, g.z));
        bouncesCount++;
        //vec3 newDir = RainScatter(currDir, pow(nextSurfaceData.refractionIndex, dispersionPow), hash);
        currDir = newDir;
        currPos += currDir * step;
      }else //refraction
      {
        /*if(fresnel > 1e-7f)
          weight *= 0.0f;*/
        currDir = GetRefractedDir(currDir, normal, H, refractionRatio);
        currPos += currDir * step;

        float OoN = abs(dot(currDir, normal));
        float G = G1_Smith(VoN, surfaceData.roughness) * G1_Smith(OoN, surfaceData.roughness);
        //weight = VoH * G / (VoN * HoN); //converges too slowly, although this multiplier is needed for correctness
        
      }

      transparency *= weight;

      prevSurfaceData = surfaceData;
      prevDensity = density;

      //transparency *= exp(-volumeSample.x * step * opacity);
      //transparency *= weight;
      if(any(lessThan(currPos, volumeAABB.minPoint) ) || any(greaterThan(currPos, volumeAABB.maxPoint) ) || all(lessThan(transparency, vec3(0.01))) )
        done = true;
    }
    
  }
  
  //vec4 cubemapSample = vec4(pow(max(0.0f, dot(currDir, vec3(1.0f, 0.0f, 0.0f))), 2500.0f)) * 300.0f;
  vec4 cubemapSample = textureLod(specularCubemap, currDir, 0);
  emission += cubemapSample.rgb * transparency * dispersedColor;// * debug;
  
  return emission;


}
void main() 
{
  /*outColor = vec4(texture(preintReflectSampler, vec3(fragScreenCoord.xy, 0.5f), 0).aaa * 0.8f + 0.1f, 1.0f);
  return;*/
  /*vec4 directLight = textureLod(directLightSampler, fragScreenCoord, 0);  
  vec4 albedoColor = textureLod(albedoSampler, fragScreenCoord, 0);*/  
  ivec2 viewportSize2i = ivec2(viewportSize.xy + vec2(0.5f));
  ivec2 pixelCoord = ivec2(fragScreenCoord.xy * viewportSize.xy);
  

  mat4 invViewMatrix = inverse(viewMatrix);
  vec3 camPos = (invViewMatrix * vec4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;

  mat4 viewProjMatrix = projMatrix * viewMatrix;
  mat4 invViewProjMatrix = inverse(viewProjMatrix);

  vec3 rayDir = normalize(Unproject(vec3(fragScreenCoord.xy, 1.0f), invViewProjMatrix));

  /*{
    vec3 resDir = rayDir;

    Ray currRay;
    currRay.dir = rayDir;
    
    mat3 tbn = MakeTbnBasis(rayDir);
    
    
    vec3 localOrigin;
    localOrigin.xy = (fragScreenCoord.xy - vec2(0.5f, 0.5f)) * 2.0f;
    float r2 = max(0.0f, dot(localOrigin.xy, localOrigin.xy));
    vec3 resWeight = vec3(1.0f);
    if(r2 < 1.0f)
    {
      localOrigin.z = -sqrt(1.0f - r2);
      currRay.origin = localOrigin;
      resDir = GetSphereScatter(1.33f, currRay, 1.0f, vec3(time, fragScreenCoord.xy));
      float eps = 1e-3f;
      vec3 offsetDir = GetSphereScatter(1.33f + eps, currRay, 1.0f, vec3(time, fragScreenCoord.xy));
      
      vec3 rayDerivative = (resDir - offsetDir) / eps;
      
      vec3 hash = hash33(vec3(fragScreenCoord.xy, time));
      resDir = normalize(resDir + rayDerivative * hash.x * 1e-1f);
      resWeight = GetWavelengthRGB(hash.x);
    }
    vec3 emission = textureLod(specularCubemap, resDir, 0).rgb * resWeight;
    if(resetBuffer > 0.5f)
      outColor = vec4(emission, 1.0f);
    else
      outColor = vec4(emission / (totalWeight + 1.0f), 1.0f - totalWeight / (totalWeight + 1.0f));
    return;
  }*/

 
  vec3 emission = CastRay(camPos, rayDir, vec3(time, fragScreenCoord.xy));
  
  if(resetBuffer > 0.5f)
    outColor = vec4(emission, 1.0f);
  else
    outColor = vec4(emission / (totalWeight + 1.0f), 1.0f - totalWeight / (totalWeight + 1.0f));
  /*float exposure = 1.0f;
  outColor = vec4(vec3(1.0f) - exp(-emission * exposure), 1.0f);*/
}