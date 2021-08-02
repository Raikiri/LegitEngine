#pragma once
class ListBucketeer
{
public:
  ListBucketeer(legit::Core *_core)
  {
    this->core = _core;

    ReloadShaders();
  }
public:
  struct BucketBuffers
  {
    legit::RenderGraph::BufferProxyId bucketsProxyId;
    legit::RenderGraph::BufferProxyId mipInfosProxyId;
    legit::RenderGraph::BufferProxyId pointsListProxyId;
    legit::RenderGraph::BufferProxyId blockPointsListProxyId;
    size_t totalBucketsCount;
  };

  /*void ResizeViewport(glm::uvec2 viewportSize, legit::RenderGraph *renderGraph, size_t framesInFlightCount, Scene *scene)
  {
    this->viewportSize = viewportSize;
    this->renderGraph = renderGraph;

    this->pointsCount = 0;
    scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer, vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)
    {
      pointsCount += verticesCount;
    });

    pointBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), sizeof(Point) * pointsCount, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal));

    glm::uvec2 viewportSize = { viewportExtent.width, viewportExtent.height };
    viewportResources.reset(new SwapchainResources(renderGraph, viewportSize, glm::vec2(512, 512), pointBuffer.get()));

    //this->viewportSize = _viewportSize;
  }*/
  void RecreateSwapchainResources(glm::uvec2 viewportSize, size_t framesInFlightCount, size_t maxMipsCount = std::numeric_limits<size_t>::max())
  {
    viewportResources.reset(new ViewportResources(core, viewportSize, maxMipsCount));
  }

  void RecreateSceneResources(size_t pointsCount)
  {
    sceneResources.reset(new SceneResources(core, pointsCount));
  }

  BucketBuffers BucketPoints(legit::ShaderMemoryPool *memoryPool, glm::mat4 projMatrix, glm::mat4 viewMatrix, legit::RenderGraph::BufferProxyId pointDataProxyId, uint32_t pointsCount, bool sort)
  {
    assert(viewportResources);
    vk::Extent2D viewportExtent = vk::Extent2D(viewportResources->viewportSize.x, viewportResources->viewportSize.y);
    PassData passData;
    passData.viewMatrix = viewMatrix;
    passData.projMatrix = projMatrix;
    passData.sortDir = glm::inverse(viewMatrix) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    passData.mipsCount = glm::uint(viewportResources->mipsCount);
    passData.totalBucketsCount = glm::uint(viewportResources->totalBucketsCount);
    passData.time = 0.0f;
    passData.debugMip = -1;
    passData.debugType = -1;

    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageBuffers({
        viewportResources->bucketsProxy->Id(),
        viewportResources->mipInfosProxy->Id() })
      .SetProfilerInfo(legit::Colors::emerald, "PassBcrClean")
      .SetRecordFunc([this, memoryPool, passData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = bucketingShaders.clearShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassDataBuffer");
          *shaderPassDataBuffer = passData;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));
        auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        size_t workGroupSize = shader->GetLocalSize().x;
        passContext.GetCommandBuffer().dispatch(uint32_t(viewportResources->totalBucketsCount / (workGroupSize) + 1), 1, 1);
      }
    }));


    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      /*.SetColorAttachments({ 
        { viewportResources->tmpBucketTexture.imageViewProxy->Id() } })*/
      .SetStorageBuffers({
        viewportResources->bucketsProxy->Id(),
        viewportResources->mipInfosProxy->Id(),
        sceneResources->pointsListProxy->Id(),
        pointDataProxyId })
      .SetRenderAreaExtent(viewportExtent)
      .SetProfilerInfo(legit::Colors::carrot, "PassBcrFill")
      .SetRecordFunc([this, passData, memoryPool, pointDataProxyId, pointsCount](legit::RenderGraph::RenderPassContext passContext)
    {
      std::vector<legit::BlendSettings> attachmentBlendSettings;
      attachmentBlendSettings.resize(passContext.GetRenderPass()->GetColorAttachmentsCount(), legit::BlendSettings::Opaque());
      auto shaderProgram = bucketingShaders.fillShader.program.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), attachmentBlendSettings, legit::VertexDeclaration(), vk::PrimitiveTopology::ePointList, shaderProgram);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto passDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassDataBuffer");
          *passDataBuffer = passData;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));
        auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));
        auto pointsListBuffer = passContext.GetBuffer(sceneResources->pointsListProxy->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));
        auto pointsBuffer = passContext.GetBuffer(pointDataProxyId);
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsBuffer));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
          { shaderDataSet },
          { shaderData.dynamicOffset });

        passContext.GetCommandBuffer().draw(pointsCount, 1, 0, 0);
      }
    }));

    if(sort)
    {
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({ 
          viewportResources->bucketsProxy->Id(),
          viewportResources->mipInfosProxy->Id(),
          sceneResources->pointsListProxy->Id() })
        .SetProfilerInfo(legit::Colors::amethyst, "PassBcrSorting")
        .SetRecordFunc([this, passData, memoryPool, pointDataProxyId](legit::RenderGraph::PassContext passContext)
      {
        std::vector<legit::BlendSettings> attachmentBlendSettings;
        auto shader = sortShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto passDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassDataBuffer");
            *passDataBuffer = passData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));
          auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));
          auto pointsListBuffer = passContext.GetBuffer(sceneResources->pointsListProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          size_t workGroupSize = shader->GetLocalSize().x;
          passContext.GetCommandBuffer().dispatch(uint32_t(viewportResources->totalBucketsCount / (workGroupSize) + 1), 1, 1);
        }
      }));
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({ 
          viewportResources->bucketsProxy->Id(),
          viewportResources->mipInfosProxy->Id(),
          sceneResources->pointsListProxy->Id(),
          sceneResources->blockPointsListProxy->Id() })
        .SetProfilerInfo(legit::Colors::orange, "PassBlckSorting")
        .SetRecordFunc([this, passData, memoryPool, pointDataProxyId](legit::RenderGraph::PassContext passContext)
      {
        std::vector<legit::BlendSettings> attachmentBlendSettings;
        auto shader = blockSortShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto passDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassDataBuffer");
            *passDataBuffer = passData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));
          auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));
          auto pointsListBuffer = passContext.GetBuffer(sceneResources->pointsListProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));
          auto blockPointsListBuffer = passContext.GetBuffer(sceneResources->blockPointsListProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BlockPointsListBuffer", blockPointsListBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          size_t workGroupSize = shader->GetLocalSize().x;
          passContext.GetCommandBuffer().dispatch(uint32_t(viewportResources->totalBucketsCount / (workGroupSize) + 1), 1, 1);
        }
      }));

      
    }
    BucketBuffers res;
    res.bucketsProxyId = viewportResources->bucketsProxy->Id();
    res.mipInfosProxyId = viewportResources->mipInfosProxy->Id();
    res.pointsListProxyId = sceneResources->pointsListProxy->Id();
    res.blockPointsListProxyId = sceneResources->blockPointsListProxy->Id();
    res.totalBucketsCount = viewportResources->totalBucketsCount;

    return res;
  }

  void ReloadShaders()
  {

    bucketingShaders.fillShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ListBucketeer/PointBuckets/pointRasterizer.vert.spv"));
    bucketingShaders.fillShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ListBucketeer/PointBuckets/pointBucketsFill.frag.spv"));
    bucketingShaders.fillShader.program.reset(new legit::ShaderProgram(bucketingShaders.fillShader.vertex.get(), bucketingShaders.fillShader.fragment.get()));

    bucketingShaders.clearShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ListBucketeer/PointBuckets/pointBucketsClear.comp.spv"));
    sortShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ListBucketeer/PointBuckets/bucketSort.comp.spv"));
    blockSortShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ListBucketeer/PointBuckets/blockSort.comp.spv"));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;


  struct ViewportResources
  {
    ViewportResources(legit::Core *core, glm::uvec2 viewportSize, size_t maxMipsCount)
    {
      this->viewportSize = viewportSize;
      this->mipsCount = 0;
      this->totalBucketsCount = 0;
      std::vector<MipInfo> mipInfosData;
      for (
        glm::uvec2 currMipSize = viewportSize;
        currMipSize.x > 0 && currMipSize.y > 0 && this->mipsCount < maxMipsCount;
        currMipSize.x /= 2, currMipSize.y /= 2)
      {
        MipInfo mipInfo;
        mipInfo.size = glm::ivec4(currMipSize.x, currMipSize.y, 0, 0);
        mipInfo.bucketIndexOffset = glm::uint(totalBucketsCount);
        mipInfosData.push_back(mipInfo);

        totalBucketsCount += currMipSize.x * currMipSize.y;
        this->mipsCount++;
      }
      size_t mipInfosSize = sizeof(MipInfo) * mipInfosData.size();
      mipInfosBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), mipInfosSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal));
      legit::LoadBufferData(core, mipInfosData.data(), mipInfosSize, mipInfosBuffer.get());

      this->bucketsProxy = core->GetRenderGraph()->AddBuffer<Bucket>(uint32_t(totalBucketsCount));
      this->mipInfosProxy = core->GetRenderGraph()->AddExternalBuffer(mipInfosBuffer.get());
    }

    legit::RenderGraph::BufferProxyUnique bucketsProxy;
    legit::RenderGraph::BufferProxyUnique mipInfosProxy;

    std::unique_ptr<legit::Buffer> mipInfosBuffer;
    glm::uvec2 viewportSize;
    size_t totalBucketsCount;
    size_t mipsCount;
  };
  std::unique_ptr<ViewportResources> viewportResources;

  struct SceneResources
  {
    SceneResources(legit::Core *core, size_t pointsCount)
    {
      this->pointsListProxy = core->GetRenderGraph()->AddBuffer<PointNode>(uint32_t(pointsCount));
      this->blockPointsListProxy = core->GetRenderGraph()->AddBuffer<BlockPointNode>(uint32_t(pointsCount));
    }
    legit::RenderGraph::BufferProxyUnique pointsListProxy;
    legit::RenderGraph::BufferProxyUnique blockPointsListProxy;
  };
  std::unique_ptr<SceneResources> sceneResources;

  #pragma pack(push, 1)
  struct PassData
  {
    glm::mat4 viewMatrix; //world -> camera
    glm::mat4 projMatrix; //camera -> ndc
    glm::vec4 sortDir;
    glm::uint mipsCount;
    glm::uint totalBucketsCount;
    float time;
    int debugMip;
    int debugType;
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct MipInfo
  {
    glm::ivec4 size;
    glm::uint bucketIndexOffset;
    float debug;
    float padding[2];
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct Bucket
  {
    glm::uint headPointIndex;
    glm::uint pointsCount;
    glm::uint blockHeadPointIndex;
    float padding;
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct PointNode
  {
    glm::uint nextPointIndex;
    float dist;
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct BlockPointNode
  {
    struct PointListNode
    {
      glm::uint nextPointIndex;
    };
    PointListNode nextPointIndex[4];
  };
  #pragma pack(pop)

  struct BucketingShader
  {
    struct ClearShader
    {
      std::unique_ptr<legit::Shader> compute;
    } clearShader;

    struct FillShader
    {
      std::unique_ptr<legit::Shader> vertex;
      std::unique_ptr<legit::Shader> fragment;
      std::unique_ptr<legit::ShaderProgram> program;
    } fillShader;
  }bucketingShaders;

  struct SortShader
  {
    std::unique_ptr<legit::Shader> compute;
  } sortShader;

  struct BlockSortShader
  {
    std::unique_ptr<legit::Shader> compute;
  } blockSortShader;

  std::unique_ptr<legit::Sampler> screenspaceSampler;

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