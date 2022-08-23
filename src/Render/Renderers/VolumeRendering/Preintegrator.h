
struct Preintegrator
{
  Preintegrator()
  {
    size_t fieldResolution = 32;
    size_t angResolution = 16;

    this->preintReflectTex.format = vk::Format::eR32G32B32A32Sfloat;
    this->preintReflectTex.texelSize = sizeof(float) * 4;
    this->preintReflectTex.baseSize = glm::uvec3(fieldResolution, fieldResolution, angResolution);
    this->preintReflectTex.layersCount = 1;
    size_t mipsCount = 1;
    this->preintReflectTex.layersCount = 1;
    this->preintReflectTex.mips.resize(mipsCount);
    this->preintReflectTex.mips[0].layers.resize(this->preintReflectTex.layersCount);
    this->preintReflectTex.mips[0].layers[0].offset = 0;
    this->preintReflectTex.mips[0].size = this->preintReflectTex.baseSize;


    size_t totalSize = (preintReflectTex.baseSize.x * preintReflectTex.baseSize.y * preintReflectTex.baseSize.z) * preintReflectTex.texelSize;
    this->preintReflectTex.texels.resize(totalSize);

    this->preintScatterTex = preintReflectTex;

    /*float VoH = 0.078125;
    float start = 0.476563;
    float end = 0.835938;

    float t0 = GetPreintData<T>(start, end, VoH, 1).reflectProb;
    float t1 = GetPreintData<T>(start, end, VoH, 10).reflectProb;
    float t2 = GetPreintData<T>(start, end, VoH, 100).reflectProb;
    float t3 = GetPreintData<T>(start, end, VoH, 1000).reflectProb;
    float t4 = GetPreintData<T>(start, end, VoH, 10000).reflectProb;
    float n1 = TransferFunction(start).refractionIndex;
    float n2 = TransferFunction(end).refractionIndex;
    float ref = FresnelFull(VoH, n1, n2);
    printf("1: %f, 2: %f, 3: %f, 4: %f, 5: %f, ref: %f\n", t0, t1, t2, t3, t4, ref);*/
    for (int i = 0; i < fieldResolution; i++)
    {
      for (int j = 0; j < fieldResolution; j++)
      {
        for (int ang = 0; ang < angResolution; ang++)
        {
          float startField = float(i + 0.5f) / float(fieldResolution);
          float endField = float(j + 0.5f) / float(fieldResolution);
          //float VoH = float(ang + 0.5f) / float(angResolution);
          float sinAng = float(ang + 0.5f) / float(angResolution);;
          float VoH = sqrt(1.0f - sinAng * sinAng);
          PreintData preintData = GetPreintData<double>(startField, endField, VoH, 100);

          size_t offset = (i + j * fieldResolution + ang * fieldResolution * fieldResolution) * sizeof(glm::vec4);
          glm::vec4 *reflectData = (glm::vec4*)(this->preintReflectTex.texels.data() + offset);
          glm::vec4 *scatterData = (glm::vec4*)(this->preintScatterTex.texels.data() + offset);

          /*float n1 = TransferFunction(startField).refractionIndex;
          float n2 = TransferFunction(endField).refractionIndex;
          float cmp = FresnelFull(VoH, n1, n2);
          if (abs(cmp - preintData.reflectProb) > 1e-1f)
          {
            printf("err: %f != %f VoH: %f, t1: %f, t2: %f, n1: %f, n2: %f\n", cmp, preintData.reflectProb, VoH, startField, endField, n1, n2);
          }*/
          reflectData->x = preintData.reflectRoughness;
          reflectData->y = preintData.refractionRatio;
          reflectData->z = 0;
          reflectData->w = preintData.reflectProb;
          scatterData->x = preintData.scatterColor.x;
          scatterData->y = preintData.scatterColor.y;
          scatterData->z = preintData.scatterColor.z;
          scatterData->w = preintData.scatterProb;
        }
      }
    }
  }
  struct SurfaceData
  {
    glm::vec3 diffuseColor;
    float density;
    float refractionIndex;
    float roughness;
    float metalness;
  };

  template<typename T>
  T saturate(T val)
  {
    return (val < 0.0f) ? 0.0f : ((val > 1.0f) ? 1.0f : val);
  }

  template<typename T>
  T lerp(T a, T b, T t)
  {
    return a * (T(1.0) - t) + b * t;
  }
  SurfaceData TransferFunction(float volumeSample)
  {
    glm::vec3 blue = glm::vec3(0.0, 0.5, 0.75);
    glm::vec3 orange = glm::vec3(0.8f, 0.4f, 0.1f);
    glm::vec3 red = glm::vec3(pow(0.8f, 2.2f), pow(0.25f, 2.2f), pow(0.1f, 2.2f));

    SurfaceData resData;
    float boneRatio = saturate((volumeSample - 0.95f) * 10000.0f);
    float glazeRatio = saturate((volumeSample - 0.5f) * 1000.0f + 0.0f);
    float veinRatio = saturate((volumeSample - 0.58f) * 1.0f) * (1.0f - boneRatio);
    float fleshRatio = saturate((volumeSample - 0.3f) * 1.0f) * (1.0f - veinRatio);
    float filmRatio = std::max(0.0f, 1.0f - abs(volumeSample - 0.5f) * 1000.0f);
    //float skinRatio = saturate((volumeSample - 0.005f) * 50.0f);
    float skinRatio = saturate((volumeSample - 0.45f) * 1000.0f);
    float skinSmoothRatio = saturate((volumeSample - 0.014f) * 1000.0f - 3.0f);
    float spaceRatio = 1.0f;


    float filmDensity = 50000.0f * filmRatio * 0.0f;
    float boneDensity = 2000.0f * boneRatio;
    float veinDensity = 1000.0f * veinRatio * 0.0f;
    float fleshDensity = 300.0f * fleshRatio * 0.0f;
    float skinDensity = 0.0f * skinRatio;
    float spaceDensity = 0.0f * spaceRatio;

    resData.density = (boneDensity + veinDensity + fleshDensity + skinDensity + spaceDensity + filmDensity);
    resData.diffuseColor = (glm::vec3(0.95f, 0.92f, 0.85f) * boneDensity + orange * veinDensity + red/*blue*/ * fleshDensity + red * filmDensity) / (resData.density + 1e-9f);// ;
                                                                                                                                                                            //resData.density += saturate((volumeSample - 0.015f) * 100.0f) * 30.0f;
    resData.refractionIndex = 1.0f;
    resData.refractionIndex += (fleshRatio > 0.0f ? 1.0f : 0.0f) * 0.33f * 0.0f + (skinRatio > 0.0f ? 1.0f : 0.0f) * 0.33f * 0.0f + (veinRatio > 0.0f ? 1.0f : 0.0f) * 0.33f * 0.0f + (boneRatio > 0.0f ? 1.0f : 0.0f) * 0.33f;
    //resData.refractionIndex += saturate(-(volumeSample.x - 0.015f) * 100.0f) * 0.33f + 1.0f;
    resData.roughness = 0.5f;//0.5f * (1.0f - glazeRatio) * 0.05f;
    resData.metalness = 0.0f;//glazeRatio > 0.5f ? 1.0f : 0.0f;
    return resData;
  }



  float FresnelFull(float VoH, float n1, float n2)
  {
    //https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
    float c = VoH;
    float eta = n2 / n1;
    float g2 = eta * eta - 1.0f + c * c;
    if (g2 < 0.0f)
      return 1.0f;

    float g = sqrt(g2);
    float a = 0.5f * (g - c) * (g - c) / ((g + c) * (g + c));
    float b1 = c * (g + c) - 1.0f;
    float b2 = c * (g - c) + 1.0f;
    return a * (1.0f + b1 * b1 / (b2 * b2));

    /*float f0 = pow(abs((n1 - n2) / (n1 + n2)), 2.0f);
    return f0 + (1.0f - f0) * Fresnel(VoH);*/
  }

  float G1_Smith(float NoV, float roughness)
  {
    float alpha = roughness * roughness;
    float k = alpha / 2.0f;
    return NoV / (NoV * (1.0f - k) + k);
  }


  glm::vec3 ImportanceSampleGGX(glm::vec2 uniformSeed, float roughness, glm::mat3 tbnBasis)
  {
    float alpha = roughness * roughness;
    float phi = 2.0f * 3.141592f * uniformSeed.x;
    float cosTheta = sqrt((1 - uniformSeed.y) / (1 + (alpha * alpha - 1) * uniformSeed.y));
    float sinTheta = sqrt(1 - cosTheta * cosTheta);
    glm::vec3 localH;
    localH.x = sinTheta * cos(phi);
    localH.y = sinTheta * sin(phi);
    localH.z = cosTheta;

    return tbnBasis * localH;
    //pdf of reflected vector = D * NoH / (4 * VoH) 
  }
  float random()
  {
    return float(rand()) / float(RAND_MAX);
  }
  struct PreintData
  {
    float reflectRoughness;
    float refractionRatio;
    float reflectFull;
    float scatterProb;
    glm::vec3 scatterColor;
    float reflectProb;
  };

  float GetFullFresnelProb(float VoN, float n1, float n2, float roughness)
  {
    glm::vec3 N = { 0.0f, 1.0f, 0.0f };
    glm::mat3 tbnBasis;
    tbnBasis[0] = { 1.0f, 0.0f, 0.0f };
    tbnBasis[1] = { 0.0f, 0.0f, 1.0f };
    tbnBasis[2] = N;
    glm::vec3 V;
    V.x = 0;
    V.y = VoN;
    V.z = -sqrt(1.0f - VoN * VoN);

    float sumProb = 0.0f;
    float sumWeight = 0.0f;

    float step = 0.1f;
    glm::vec2 randParam;
    for (randParam.y = step / 2.0f; randParam.y < 1.0f; randParam.y += step)
    {
      for (randParam.x = step / 2.0f; randParam.x < 1.0f; randParam.x += step)
      {
        glm::vec3 H = ImportanceSampleGGX(glm::vec2(randParam.x, randParam.y), roughness, tbnBasis);
        glm::vec3 L = 2.0f * dot(V, H) * H - V;


        float VoH = abs(glm::dot(V, H));
        float VoN = abs(glm::dot(V, N));
        float HoN = abs(glm::dot(H, N));

        float fresnel = 0;
        if (VoH * VoN > 1e-7f)
        {
          fresnel = FresnelFull(VoH, n1, n2);

          //fresnel /= D(NoH) * NoH / (4 * VoH);
        }
        sumProb += fresnel;
        sumWeight += 1.0f;
      }
    }
    return sumProb / (sumWeight + 1e-9f);
  }
  template<typename T>
  PreintData GetPreintData(T startField, T endField, T VoH, int substepsCount)
  {
    T totalReflectProbability = 0;
    T totalScatterProbability = 0.0f;
    glm::vec3 scatterColor = glm::vec3(0.0f);
    T reflectRoughness = 0.0f;


    T stepSize = 1.0e-2;
    T substepSize = stepSize / substepsCount;
    SurfaceData prevSurfaceData = TransferFunction(float(startField));
    for (int substepIndex = 1; substepIndex <= substepsCount; substepIndex++)
    {
      T ratio = T(substepIndex) / T(substepsCount);
      T field = lerp(T(startField), T(endField), ratio);
      SurfaceData surfaceData = TransferFunction(float(field));



      T fresnel = FresnelFull(float(VoH), prevSurfaceData.refractionIndex, surfaceData.refractionIndex);// GetFullFresnelProb(VoN, prevSurfaceData.refractionIndex, surfaceData.refractionIndex, surfaceData.roughness);
      T scatter = 1.0 - exp(-surfaceData.density * substepSize);

      T remainingProb = (1.0 - totalReflectProbability - totalScatterProbability);
      T reflectProb = remainingProb * fresnel;
      if(totalReflectProbability + reflectProb > 0)
        reflectRoughness = (reflectRoughness * totalReflectProbability + reflectProb * surfaceData.roughness) / (totalReflectProbability + reflectProb);
      totalReflectProbability += reflectProb;

      remainingProb = (1.0 - totalReflectProbability - totalScatterProbability);
      T scatterProb = remainingProb * scatter;
      if(float(totalScatterProbability + scatterProb) > 0)
      scatterColor = (scatterColor * float( totalScatterProbability) + float(scatterProb) * surfaceData.diffuseColor) / float(totalScatterProbability + scatterProb);
      totalScatterProbability += scatterProb;

      /*float sinAlpha = sqrt(1.0f - VoH * VoH);
      float newSinAlpha = sinAlpha * prevSurfaceData.refractionIndex / surfaceData.refractionIndex;
      if (newSinAlpha > 1.0f)
        totalReflectProbability = 1.0f - totalScatterProbability;
      else
      {
        VoH = sqrt(1.0f - newSinAlpha * newSinAlpha);
      }*/

      //T fresnel = FresnelFull(VoH, prevSurfaceData.refractionIndex, surfaceData.refractionIndex);
      /*if (prevSurfaceData.refractionIndex < 1.3f && surfaceData.refractionIndex > 1.3f)
        fresnel = 1.0f;
      else
        fresnel = 0.0f;*/
      //T scatter = 1.0f - exp(-surfaceData.density * substepSize);

      /*T remainingProb = (1.0f - totalReflectProbability) * (1.0f - totalScatterProbability);
      T reflectProb = remainingProb * fresnel;
      T scatterProb = remainingProb * scatter;

      scatterColor = (scatterColor * totalScatterProbability + scatterProb * surfaceData.diffuseColor) / (totalScatterProbability + scatterProb + 1e-5f);
      totalReflectProbability += reflectProb;
      totalScatterProbability += scatterProb;*/
      /*T remainingProb = (1.0f - totalReflectProbability - totalScatterProbability);
      T reflectProb = remainingProb * fresnel;
      totalReflectProbability += reflectProb;

      remainingProb = (1.0f - totalReflectProbability - totalScatterProbability);
      T scatterProb = remainingProb * scatter;
      scatterColor = (scatterColor * totalScatterProbability + scatterProb * surfaceData.diffuseColor) / (totalScatterProbability + scatterProb + 1e-5f);
      totalScatterProbability += scatterProb;*/

      prevSurfaceData = surfaceData;
    }

    PreintData res;
    res.reflectRoughness = float(reflectRoughness);
    res.refractionRatio = TransferFunction(float(startField)).refractionIndex / TransferFunction(float(endField)).refractionIndex;
    res.reflectProb = float(totalReflectProbability);
    res.scatterColor = scatterColor;
    res.scatterProb = float(totalScatterProbability);
    return res;
  }
  legit::ImageTexelData preintReflectTex;
  legit::ImageTexelData preintScatterTex;
};
