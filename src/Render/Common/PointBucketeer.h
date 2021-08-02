class PointBucketeer
{
public:
  PointBucketeer(legit::Core *_core)
  {
    this->core = _core;

    ReloadShaders();
    ResetCaches();
  }
  void ResizeViewport(vk::Extent2D _viewportSize)
  {
    ResetCaches();
    //this->viewportSize = _viewportSize;
  }
private:
  
  struct FrameResources;
public:
  void BucketPoints(legit::Core *core, legit::RenderGraph *renderGraph, legit::ShaderMemoryPool *memoryPool, glm::uvec2 viewportSize, glm::mat4 projMatrix, glm::mat4 viewMatrix, legit::RenderGraph::BufferProxy pointDataProxy, uint32_t maxPointsCount)
  {
    auto &frameResourcesUnique = frameResourcesDatum[renderGraph];

    if (!frameResourcesUnique)
    {
      frameResourcesUnique.reset(new FrameResources(core, renderGraph, viewportSize, pointData, maxPointsCount));
    }
    FrameResources *frameResources = frameResourcesUnique.get();

    std::unique_ptr<legit::Buffer> mipInfoBuffer;

    PassData passData;
    passData.viewMatrix = viewMatrix;
    passData.projMatrix = projMatrix;
    passData.mipsCount = frameResources->mipsCount;
    passData.totalBucketsCount = frameResources->totalBucketsCount;
    passData.time = 0.0f;

    for(int phase = 0; phase < 2; phase++)
    {
      renderGraph->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({
          frameResources->bucketDataProxy,
          frameResources->mipInfosProxy,
          pointDataProxy })
        .SetProfilerInfo(legit::Colors::emerald, phase == 0 ? "PassBcrClean" : "PassBcrAlloc")
        .SetRecordFunc([this, memoryPool, frameResources, passData, phase](legit::RenderGraph::PassContext passContext)
      {
        auto shader = phase == 0 ? clearShader.comp.get() : allocShader.comp.get();
        auto pipeineInfo = pipelineCache->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
            *shaderPassDataBuffer = passData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketDataBuffer = passContext.GetBuffer(frameResources->bucketDataProxy);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketData", bucketDataBuffer));
          auto mipDataBuffer = passContext.GetBuffer(frameResources->mipInfosProxy);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipData", mipDataBuffer));

          auto shaderDataSet = descriptorSetCache->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          size_t workGroupSize = shader->GetLocalSize().x;
          passContext.GetCommandBuffer().dispatch(uint32_t(passData.totalBucketsCount / (workGroupSize)), 1, 1);
        }
      }));


      renderGraph->AddPass(legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({ 
          { frameResources->tmpBucketTexture.imageViewProxy } })
        .SetStorageBuffers({
          passData.frameResources->pointData})
        .SetRenderAreaExtent(frameInfo.viewportSize)
        .SetProfilerInfo(legit::Colors::carrot, "PassPointSprites")
        .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
      {
        std::vector<legit::BlendSettings> attachmentBlendSettings;
        attachmentBlendSettings.resize(passContext.GetRenderPass()->GetColorAttachmentsCount(), legit::BlendSettings::Opaque());
        auto shaderProgram = pointSpritesShader.program.get();
        auto pipeineInfo = pipelineCache->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::DepthTest(), attachmentBlendSettings, vertexDecl, vk::PrimitiveTopology::ePointList, shaderProgram);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<PointSpritesShader::DataBuffer>("PassData");

            shaderDataBuffer->projMatrix = passData.projMatrix;
            shaderDataBuffer->viewMatrix = passData.viewMatrix;
            shaderDataBuffer->viewportSize = glm::vec4(passData.viewportSize.width, passData.viewportSize.height, 0.0f, 0.0f);
            shaderDataBuffer->fovy = passData.fovy;
            shaderDataBuffer->pointWorldSize = passData.pointWorldSize;
            shaderDataBuffer->time = 0.0f;
          }
          passData.memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto pointDataBuffer = passContext.GetBuffer(passData.frameResources->pointData);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointData", pointDataBuffer));

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));


          auto shaderDataSet = descriptorSetCache->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);

          const legit::DescriptorSetLayoutKey *drawCallSetInfo = shaderProgram->GetSetInfo(DrawCallDataSetIndex);

          int basePointIndex = 0;
          passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer , vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)
          {
            auto drawCallData = passData.memoryPool->BeginSet(drawCallSetInfo);
            {
              auto drawCallData = passData.memoryPool->GetUniformBufferData<DrawCallDataBuffer>("DrawCallData");
              drawCallData->modelMatrix = objectToWorld;
              drawCallData->albedoColor = glm::vec4(albedoColor, 1.0f);
              drawCallData->emissiveColor = glm::vec4(emissiveColor, 1.0f);
              drawCallData->basePointIndex = basePointIndex;
            }
            passData.memoryPool->EndSet();
            basePointIndex += verticesCount;

            auto drawCallSet = descriptorSetCache->GetDescriptorSet(*drawCallSetInfo, drawCallData.uniformBufferBindings, {}, {});
            passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
              { shaderDataSet, drawCallSet },
              { shaderData.dynamicOffset, drawCallData.dynamicOffset });

            passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
            passContext.GetCommandBuffer().draw(verticesCount, 1, 0, 0);
          });
        }
      }));
    }
  }

  void ReloadShaders()
  {
    /*bucketFillShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/pointRasterizer.vert.spv"));
    bucketFillShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/bucketFill.frag.spv"));
    bucketFillShader.program.reset(new legit::ShaderProgram(bucketFillShader.vertex.get(), bucketFillShader.fragment.get()));*/

    clearShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/PointBucketeer/bucketClear.comp.spv"));
    allocShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/PointBucketeer/bucketAlloc.comp.spv"));
    mipInitShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/PointBucketeer/mipInit.comp.spv"));
  }
  void ResetCaches()
  {
    frameResourcesDatum.clear();
    descriptorSetCache.reset(new legit::DescriptorSetCache(core->GetLogicalDevice()));
    pipelineCache.reset(new legit::PipelineCache(core->GetLogicalDevice(), descriptorSetCache.get()));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;


  struct FrameResources
  {
    FrameResources(legit::Core *core, legit::RenderGraph *renderGraph, glm::uvec2 bucketBufferResolution, size_t pointsCount) :
      tmpBucketTexture(renderGraph, vk::Format::eR8G8B8A8Unorm, bucketBufferResolution)
    {
      this->maxIndicesCount = pointsCount * 4;
      this->mipsCount = 0;
      this->totalBucketsCount = 0;
      std::vector<MipInfo> mipInfosData;
      for (
        glm::uvec2 currMipSize = bucketBufferResolution;
        currMipSize.x > 0 && currMipSize.y > 0;
        currMipSize.x /= 2, currMipSize.y /= 2)
      {
        MipInfo mipInfo;
        mipInfo.size = glm::ivec4(currMipSize.x, currMipSize.y, 0, 0);
        mipInfo.bucketIndexOffset = totalBucketsCount;
        mipInfosData.push_back(mipInfo);

        totalBucketsCount += currMipSize.x * currMipSize.y;
        mipsCount++;
      }
      size_t mipInfosSize = sizeof(MipInfo) * mipInfosData.size();
      mipInfosBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), mipInfosSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal));
      legit::LoadBufferData(core, mipInfosData.data(), mipInfosSize, mipInfosBuffer.get());


      this->bucketDataProxy = renderGraph->AddBuffer<Bucket>(bucketBufferResolution.x * bucketBufferResolution.y);
      this->indexPoolDataProxy = renderGraph->AddBuffer<uint32_t>(maxIndicesCount);
      this->mipInfosProxy = renderGraph->AddExternalBuffer(mipInfosBuffer.get());
    }

    UnmippedProxy tmpBucketTexture;
    legit::RenderGraph::BufferProxy indexPoolDataProxy;
    legit::RenderGraph::BufferProxy bucketDataProxy;
    legit::RenderGraph::BufferProxy mipInfosProxy;

    std::unique_ptr<legit::Buffer> mipInfosBuffer;

    size_t totalBucketsCount;
    size_t mipsCount;
    size_t maxIndicesCount;
  };

  #pragma pack(push, 1)
  struct PassData
  {
    glm::mat4 viewMatrix; //world -> camera
    glm::mat4 projMatrix; //camera -> ndc
    glm::uint mipsCount;
    glm::uint totalBucketsCount;
    float time;
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct MipInfo
  {
    glm::ivec4 size;
    glm::uint bucketIndexOffset;
    float padding[3];
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct Bucket
  {
    glm::uint indexOffset;
    glm::uint pointsCount;
  };
  #pragma pack(pop)

  struct ClearShader
  {
    std::unique_ptr<legit::Shader> comp;
    std::unique_ptr<legit::Shader> mipInitCompShader;
  } clearShader;

  struct AllocShader
  {
    std::unique_ptr<legit::Shader> comp;
  } allocShader;
  

  //vk::Extent2D viewportSize;
  std::map<legit::RenderGraph*, std::unique_ptr<FrameResources> > frameResourcesDatum;

  std::unique_ptr<legit::DescriptorSetCache> descriptorSetCache;
  std::unique_ptr<legit::PipelineCache> pipelineCache;
  std::unique_ptr<legit::Sampler> screenspaceSampler;

  std::unique_ptr<legit::Buffer> mipInfoBuffer;

  std::default_random_engine eng;
  std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };

  legit::Core *core;
};


/*using PointIndex = size_t;
struct Point
{
  float data;
  PointIndex next;
};

size_t GetListSize(Point *points, PointIndex head)
{
  size_t count = 0;
  PointIndex curr = head;
  while (curr != PointIndex(-1))
  {
    count++;
    curr = points[curr].next;
  }
  return count;
}

struct MergeResult
{
  PointIndex head;
  PointIndex tail;
};

MergeResult MergeLists(Point *points, PointIndex leftHead, PointIndex rightHead)
{
  assert(leftHead != PointIndex(-1) && rightHead != PointIndex(-1));
  PointIndex currLeft = leftHead;
  PointIndex currRight = rightHead;
  MergeResult res;
  if (points[currLeft].data < points[currRight].data)
  {
    res.head = currLeft;
    currLeft = points[currLeft].next;
  }
  else
  {
    res.head = currRight;
    currRight = points[currRight].next;
  }
  PointIndex currMerged = res.head;
  while (currLeft != PointIndex(-1) || currRight != PointIndex(-1))
  {
    //itCount++; //~10000 iterations for random 1000-element array
    if (currRight == PointIndex(-1) || (currLeft != PointIndex(-1) && points[currLeft].data < points[currRight].data))
    {
      points[currMerged].next = currLeft;
      currMerged = currLeft;
      currLeft = points[currLeft].next;
    }
    else
    {
      points[currMerged].next = currRight;
      currMerged = currRight;
      currRight = points[currRight].next;
    }
  }
  points[currMerged].next = PointIndex(-1);
  res.tail = currMerged;
  return res;
}

PointIndex SeparateList(Point *points, PointIndex head, size_t count)
{
  assert(head != PointIndex(-1));
  PointIndex curr = head;
  for (size_t i = 0; i < count - 1 && points[curr].next != PointIndex(-1); i++)
    curr = points[curr].next;
  PointIndex nextHead = points[curr].next;
  points[curr].next = PointIndex(-1);
  return nextHead;
}

PointIndex MergeSort(Point *points, PointIndex head)
{
  size_t count = GetListSize(points, head);
  for (size_t gap = 1; gap < count; gap *= 2)
  {
    PointIndex lastTail = PointIndex(-1);
    PointIndex curr = head;
    while (curr != PointIndex(-1))
    {
      PointIndex leftHead = curr;
      PointIndex rightHead = SeparateList(points, leftHead, gap);
      if (rightHead == PointIndex(-1))
        break;

      PointIndex nextHead = SeparateList(points, rightHead, gap);

      MergeResult mergeResult = MergeLists(points, leftHead, rightHead);
      assert(mergeResult.head != PointIndex(-1));
      assert(mergeResult.tail != PointIndex(-1));
      if (lastTail != PointIndex(-1))
        points[lastTail].next = mergeResult.head;
      points[mergeResult.tail].next = nextHead;
      lastTail = mergeResult.tail;
      if (curr == head)
        head = mergeResult.head;
      curr = nextHead;
    }
  }
  return head;
}

PointIndex GetPreMinNode(Point *points, PointIndex head)
{
  PointIndex minIndex = head;
  PointIndex preMinIndex = PointIndex(-1);
  for (PointIndex curr = head; points[curr].next != PointIndex(-1); curr = points[curr].next)
  {
    if (points[points[curr].next].data < points[minIndex].data)
    {
      minIndex = points[curr].next;
      preMinIndex = curr;
    }
  }
  return preMinIndex;
}
PointIndex BubbleSort(Point *points, PointIndex head)
{
  bool wasChanged;
  do
  {
    PointIndex curr = head;
    PointIndex prev = PointIndex(-1);
    PointIndex next = points[head].next;
    wasChanged = false;
    while (next != PointIndex(-1)) 
    {
      if (points[curr].data > points[next].data)
      {
        wasChanged = true;

        if (prev != PointIndex(-1))
        {
          PointIndex tmp = points[next].next;

          points[prev].next = next;
          points[next].next = curr;
          points[curr].next = tmp;
        }
        else 
        {
          PointIndex tmp = points[next].next;

          head = next;
          points[next].next = curr;
          points[curr].next = tmp;
        }

        prev = next;
        next = points[curr].next;
      }
      else
      {
        prev = curr;
        curr = next;
        next = points[next].next;
      }
    }
  } while (wasChanged);
  return head;
}

PointIndex SortedInsert(Point *points, PointIndex head, PointIndex newPoint)
{
  if (head == PointIndex(-1) || points[head].data >= points[newPoint].data)
  {
    points[newPoint].next = head;
    head = newPoint;
  }
  else
  {
    PointIndex curr = head;
    for (; points[curr].next != PointIndex(-1) && points[points[curr].next].data < points[newPoint].data; curr = points[curr].next);
    points[newPoint].next = points[curr].next;
    points[curr].next = newPoint;
  }
  return head;
}
PointIndex InsertionSort(Point *points, PointIndex head)
{
  PointIndex newHead = PointIndex(-1);
  PointIndex curr = head;
  while (curr != PointIndex(-1))
  {
    PointIndex next = points[curr].next;
    newHead = SortedInsert(points, newHead, curr);
    curr = next;
  }
  return newHead;
}
void PrintList(Point *points, PointIndex head)
{
  size_t index = 0;
  float last = -1.0f;
  for(PointIndex curr = head; curr != PointIndex(-1); curr = points[curr].next)
  {
    std::cout << "[" << index << "]: " << points[curr].data << "\n";
    if (last > points[curr].data)
      std::cout << "\n\nFAYYYUUUL\n\n";
    last = points[curr].data;
    index++;
  }
}
void TestSort()
{
  std::default_random_engine eng;
  std::uniform_real_distribution<float> dis(0.0f, 1.0f);

  size_t pointsCount = 64;
  size_t trialsCount = 100000;

  std::vector<Point> points;
  for (size_t i = 0; i < pointsCount; i++)
  {
    if (points.size() > 0)
      points.back().next = points.size();
    Point point;
    point.data = dis(eng);
    point.next = PointIndex(-1);
    points.push_back(point);
  }
  using hrc = std::chrono::high_resolution_clock;

  std::vector<Point> bckpPoints = points;

  hrc::time_point t1 = hrc::now();
  float test1 = 0;
  for (int i = 0; i < trialsCount; i++)
  {
    points = bckpPoints;
    PointIndex newHead = BubbleSort(points.data(), PointIndex(0));
    test1 += points[newHead].data;
  }
  hrc::time_point t2 = hrc::now();
  float test2 = 0;
  for (int i = 0; i < trialsCount; i++)
  {
    points = bckpPoints;
    PointIndex newHead = MergeSort(points.data(), PointIndex(0));
    test2 += points[newHead].data;
  }
  hrc::time_point t3 = hrc::now();
  float test3 = 0;
  PointIndex newHead;
  for (int i = 0; i < trialsCount; i++)
  {
    points = bckpPoints;
    newHead = InsertionSort(points.data(), PointIndex(0));
    test3 += points[newHead].data;
  }
  hrc::time_point t4 = hrc::now();

  std::cout << "Bubble : " << std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() << "us\n";
  std::cout << "Merge: " << std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() << "us\n";
  std::cout << "Insertion: " << std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count() << "us\n";
  std::cout << "test1:" << test1 << " test2: " << test2 << " test3: " << test3 << "\n";

  PrintList(points.data(), newHead);
}

void TestSum()
{
  std::default_random_engine eng;
  std::uniform_int_distribution<size_t> dis(0, 10);

  size_t bucketsCount = 100;

  struct Bucket
  {
    size_t size;
    size_t offset;
  };
  std::vector<Bucket> buckets;
  buckets.resize(bucketsCount);
  for (size_t i = 0; i < buckets.size(); i++)
  {
    buckets[i].offset = 0;
    buckets[i].size = dis(eng);
    buckets[i].offset = buckets[i].size;
  }

  size_t stepSize = 1;
  for(;stepSize < buckets.size(); stepSize *= 2)
  {
    for (size_t i = 0; i + stepSize < buckets.size(); i += stepSize * 2)
    {
      buckets[i].offset = buckets[i].offset + buckets[i + stepSize].offset;
    }
  }
  stepSize /= 2;
  for (; stepSize > 0; stepSize /= 2)
  {
    for (size_t i = 0; i + stepSize < buckets.size(); i += stepSize * 2)
    {
      size_t sum = buckets[i].offset;
      buckets[i].offset = sum - buckets[i + stepSize].of?????fset;
      buckets[i + stepSize].offset = sum;
    }
  }

  int p = 1;
}*/