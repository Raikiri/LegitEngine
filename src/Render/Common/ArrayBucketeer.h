#pragma once
glm::uint GetMaxPow(size_t size)
{
  glm::uint p = 0;
  for (p = 0; (1ull << p) < size; p++);
  return p;
}

class ArrayBucketeer
{
public:
  ArrayBucketeer(legit::Core *_core)
  {
    this->core = _core;

    ReloadShaders();
  }

  void RecreateSceneResources(size_t pointsCount)
  {
    sceneResources.reset(new SceneResources(core, pointsCount)); //may need to update viewport resources here
  }

  void RecreateSwapchainResources(glm::uvec2 viewportSize, size_t framesInFlightCount)
  {
    viewportResources.reset(new ViewportResources(core, viewportSize, sceneResources->pointsCount));
  }

public:
  struct BucketBuffers
  {
    legit::RenderGraph::BufferProxyId bucketsProxyId;
    legit::RenderGraph::BufferProxyId mipInfosProxyId;
    legit::RenderGraph::BufferProxyId bucketEntriesPoolProxyId;

    legit::RenderGraph::BufferProxyId bucketGroupsProxyId;
    legit::RenderGraph::BufferProxyId groupEntriesPoolProxyId;
    size_t bucketGroupsCount;
  };

  BucketBuffers BucketPoints(legit::ShaderMemoryPool *memoryPool, glm::mat4 projMatrix, glm::mat4 viewMatrix, legit::RenderGraph::BufferProxyId pointDataProxyId, uint32_t pointsCount, bool sort)
  {
    vk::Extent2D viewportExtent = vk::Extent2D(viewportResources->viewportSize.x, viewportResources->viewportSize.y);
    PassData passData;
    passData.viewMatrix = viewMatrix;
    passData.projMatrix = projMatrix;
    passData.sortDir = glm::inverse(viewMatrix) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    passData.mipsCount = glm::uint(viewportResources->mipsCount);
    passData.totalBucketsCount = glm::uint(viewportResources->totalBucketsCount);

    passData.maxSizePow = GetMaxPow(viewportResources->totalBucketsCount);
    passData.blockSizePow = 0;
    passData.isFirstBlock = 0;

    passData.bucketGroupsCount = glm::uint(viewportResources->bucketGroupsCount);
    passData.time = 0.0f;

    for(int phase = 0; phase < 2; phase++)
    {
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({
          viewportResources->bucketsProxy->Id(),
          viewportResources->mipInfosProxy->Id(),
          viewportResources->bucketEntriesPoolProxy->Id() })
        .SetProfilerInfo(legit::Colors::emerald, phase == 0 ? "PassBcrClean" : "PassBcrAlloc")
        .SetRecordFunc([this, memoryPool, passData, phase](legit::RenderGraph::PassContext passContext)
      {
        auto shader = phase == 0 ? pointBuckets.clearShader.compute.get() : pointBuckets.allocShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
            *shaderPassDataBuffer = passData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));
          auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));
          auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->bucketEntriesPoolProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

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
          viewportResources->bucketEntriesPoolProxy->Id(),
          pointDataProxyId })
        .SetRenderAreaExtent(viewportExtent)
        .SetProfilerInfo(legit::Colors::carrot, phase == 0 ? "PassBcrCount" : "PassBcrFill")
        .SetRecordFunc([this, passData, memoryPool, pointDataProxyId, phase, pointsCount](legit::RenderGraph::RenderPassContext passContext)
      {
        std::vector<legit::BlendSettings> attachmentBlendSettings;
        attachmentBlendSettings.resize(passContext.GetRenderPass()->GetColorAttachmentsCount(), legit::BlendSettings::Opaque());
        auto shaderProgram = (phase == 0) ? pointBuckets.countShader.program.get() : pointBuckets.fillShader.program.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), attachmentBlendSettings, legit::VertexDeclaration(), vk::PrimitiveTopology::ePointList, shaderProgram);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto passDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
            *passDataBuffer = passData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

          auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

          auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->bucketEntriesPoolProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

          auto pointsDataBuffer = passContext.GetBuffer(pointDataProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});

          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
            { shaderDataSet },
            { shaderData.dynamicOffset });

          passContext.GetCommandBuffer().draw(pointsCount, 1, 0, 0);
        }
      }));
    }

    if(sort)
    {
      /*core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({
          viewportResources->bucketsProxy,
          viewportResources->mipInfosProxy,
          viewportResources->bucketEntriesPoolProxy,
          pointDataProxyId,
          viewportResources->bucketGroupsProxy,
          viewportResources->groupEntriesPoolProxy })
        .SetProfilerInfo(legit::Colors::pomegranate, "PassBcrSort2")
        .SetRecordFunc([this, memoryPool, viewportResources, passData, pointDataProxyId](legit::RenderGraph::PassContext passContext)
      {
        auto shader = sortShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
            *shaderPassDataBuffer = passData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

          auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

          auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->bucketEntriesPoolProxy);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

          auto pointsDataBuffer = passContext.GetBuffer(pointDataProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

          auto bucketGroupsBuffer = passContext.GetBuffer(viewportResources->bucketGroupsProxy);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketGroupsBuffer", bucketGroupsBuffer));

          auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->groupEntriesPoolProxy);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          size_t workGroupSize = shader->GetLocalSize().x;
          passContext.GetCommandBuffer().dispatch(uint32_t(passData.totalBucketsCount / (workGroupSize) + 1), 1, 1);
        }
      }));*/

      for (int phase = 0; phase < 2; phase++)
      {
        core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
          .SetStorageBuffers({
            viewportResources->bucketsProxy->Id(),
            viewportResources->mipInfosProxy->Id(),
            viewportResources->bucketEntriesPoolProxy->Id(),
            pointDataProxyId,
            viewportResources->bucketGroupsProxy->Id(),
            viewportResources->groupEntriesPoolProxy->Id() })
          .SetProfilerInfo(legit::Colors::sunFlower, phase == 0 ? "PassGrpClear" : "PassGrpAlloc")
          .SetRecordFunc([this, memoryPool, passData, pointDataProxyId, phase](legit::RenderGraph::PassContext passContext)
        {
          auto shader = (phase == 0) ? bucketGroups.clearShader.compute.get() : bucketGroups.allocShader.compute.get();
          auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
          {
            const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
            auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
            {
              auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
              *shaderPassDataBuffer = passData;
            }
            memoryPool->EndSet();

            std::vector<legit::StorageBufferBinding> storageBufferBindings;
            auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

            auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

            auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->bucketEntriesPoolProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

            auto bucketGroupsBuffer = passContext.GetBuffer(viewportResources->bucketGroupsProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketGroupsBuffer", bucketGroupsBuffer));

            auto groupEntriesPoolBuffer = passContext.GetBuffer(viewportResources->groupEntriesPoolProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("GroupEntriesPoolBuffer", groupEntriesPoolBuffer));

            auto pointsDataBuffer = passContext.GetBuffer(pointDataProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

            auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
            passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

            size_t workGroupSize = shader->GetLocalSize().x;
            passContext.GetCommandBuffer().dispatch(uint32_t(viewportResources->bucketGroupsCount / (workGroupSize) + 1), 1, 1);
          }
        }));

        core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
          .SetStorageBuffers({
            viewportResources->bucketGroupsProxy->Id(),
            viewportResources->groupEntriesPoolProxy->Id(),
            viewportResources->bucketsProxy->Id(),
            viewportResources->mipInfosProxy->Id(),
            viewportResources->bucketEntriesPoolProxy->Id(),
            pointDataProxyId})
          .SetProfilerInfo(legit::Colors::clouds, phase == 0 ? "PassGrpCount" : "PassGrpFill")
          .SetRecordFunc([this, memoryPool, passData, pointDataProxyId, phase](legit::RenderGraph::PassContext passContext)
        {
          auto shader = (phase == 0) ? bucketGroups.countShader.compute.get() : bucketGroups.fillShader.compute.get();
          auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
          {
            const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
            auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
            {
              auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
              *shaderPassDataBuffer = passData;
            }
            memoryPool->EndSet();

            std::vector<legit::StorageBufferBinding> storageBufferBindings;
            auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

            auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

            auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->bucketEntriesPoolProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

            auto bucketGroupsBuffer = passContext.GetBuffer(viewportResources->bucketGroupsProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketGroupsBuffer", bucketGroupsBuffer));

            auto groupEntriesPoolBuffer = passContext.GetBuffer(viewportResources->groupEntriesPoolProxy->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("GroupEntriesPoolBuffer", groupEntriesPoolBuffer));

            auto pointsDataBuffer = passContext.GetBuffer(pointDataProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

            auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
            passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

            size_t workGroupSize = shader->GetLocalSize().x;
            passContext.GetCommandBuffer().dispatch(uint32_t(viewportResources->totalBucketsCount / (workGroupSize) + 1), 1, 1);
          }
        }));
      }
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({
          viewportResources->bucketsProxy->Id(),
          viewportResources->mipInfosProxy->Id(),
          viewportResources->bucketEntriesPoolProxy->Id(),
          pointDataProxyId,
          viewportResources->bucketGroupsProxy->Id(),
          viewportResources->groupEntriesPoolProxy->Id() })
        .SetProfilerInfo(legit::Colors::amethyst, "PassBcrSort")
        .SetRecordFunc([this, memoryPool, passData, pointDataProxyId](legit::RenderGraph::PassContext passContext)
      {
        auto shader = sortShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
            *shaderPassDataBuffer = passData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

          auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

          auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->bucketEntriesPoolProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

          auto bucketGroupsBuffer = passContext.GetBuffer(viewportResources->bucketGroupsProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketGroupsBuffer", bucketGroupsBuffer));

          auto groupEntriesPoolBuffer = passContext.GetBuffer(viewportResources->groupEntriesPoolProxy->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("GroupEntriesPoolBuffer", groupEntriesPoolBuffer));

          auto pointsDataBuffer = passContext.GetBuffer(pointDataProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          size_t workGroupSize = shader->GetLocalSize().x;
          passContext.GetCommandBuffer().dispatch(uint32_t(viewportResources->totalBucketsCount / (workGroupSize) + 1), 1, 1);
        }
      }));

      /*for (glm::uint maxBlockSizePow = 1; maxBlockSizePow <= passData.maxSizePow; maxBlockSizePow++)
      {
        passData.isFirstBlock = 1;
        for (glm::uint blockSizePow = maxBlockSizePow; blockSizePow > 0; blockSizePow--)
        {
          passData.blockSizePow = blockSizePow;
          core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
            .SetStorageBuffers({
              viewportResources->bucketsProxy,
              viewportResources->mipInfosProxy,
              viewportResources->bucketEntriesPoolProxy,
              pointDataProxyId})
            .SetProfilerInfo(legit::Colors::amethyst, "PassBtncKernel")
            .SetRecordFunc([this, memoryPool, viewportResources, passData, pointDataProxyId](legit::RenderGraph::PassContext passContext)
          {
            auto shader = bitonicKernelShader.compute.get();
            auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
            {
              const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
              auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
              {
                auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<PassData>("PassData");
                *shaderPassDataBuffer = passData;
              }
              memoryPool->EndSet();

              std::vector<legit::StorageBufferBinding> storageBufferBindings;
              auto bucketsBuffer = passContext.GetBuffer(viewportResources->bucketsProxy);
              storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

              auto mipInfosBuffer = passContext.GetBuffer(viewportResources->mipInfosProxy);
              storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

              auto bucketEntriesPoolBuffer = passContext.GetBuffer(viewportResources->bucketEntriesPoolProxy);
              storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

              auto pointsDataBuffer = passContext.GetBuffer(pointDataProxyId);
              storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

              auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
              passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

              size_t workGroupSize = shader->GetLocalSize().x;
              passContext.GetCommandBuffer().dispatch(uint32_t((1 << passData.maxSizePow) / workGroupSize + 1), 1, 1);
            }
          }));

          passData.isFirstBlock = 0;
        }
      }*/

      
    }
    BucketBuffers res;
    res.bucketsProxyId = viewportResources->bucketsProxy->Id();
    res.mipInfosProxyId = viewportResources->mipInfosProxy->Id();
    res.bucketEntriesPoolProxyId = viewportResources->bucketEntriesPoolProxy->Id();

    res.bucketGroupsProxyId = viewportResources->bucketGroupsProxy->Id();
    res.groupEntriesPoolProxyId = viewportResources->groupEntriesPoolProxy->Id();
    res.bucketGroupsCount = viewportResources->bucketGroupsCount;
    return res;
  }

  void ReloadShaders()
  {
    pointBuckets.countShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/PointBuckets/pointRasterizer.vert.spv"));
    pointBuckets.countShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/PointBuckets/pointBucketsCount.frag.spv"));
    pointBuckets.countShader.program.reset(new legit::ShaderProgram(pointBuckets.countShader.vertex.get(), pointBuckets.countShader.fragment.get()));

    pointBuckets.fillShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/PointBuckets/pointRasterizer.vert.spv"));
    pointBuckets.fillShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/PointBuckets/pointBucketsFill.frag.spv"));
    pointBuckets.fillShader.program.reset(new legit::ShaderProgram(pointBuckets.fillShader.vertex.get(), pointBuckets.fillShader.fragment.get()));

    pointBuckets.clearShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/PointBuckets/pointBucketsClear.comp.spv"));
    pointBuckets.allocShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/PointBuckets/pointBucketsAlloc.comp.spv"));
    
    bucketGroups.clearShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/BucketGroups/bucketGroupsClear.comp.spv"));
    bucketGroups.countShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/BucketGroups/bucketGroupsCount.comp.spv"));
    bucketGroups.allocShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/BucketGroups/bucketGroupsAlloc.comp.spv"));
    bucketGroups.fillShader .compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/BucketGroups/bucketGroupsFill.comp.spv"));

    sortShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/bucketSort.comp.spv"));

    bitonicKernelShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/ArrayBucketeer/bitonicKernel.comp.spv"));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  struct SceneResources
  {
    SceneResources(legit::Core *core, size_t pointsCount)
    {
      this->pointsCount = pointsCount;
    }
    size_t pointsCount;
  };
  std::unique_ptr<SceneResources> sceneResources;

  struct ViewportResources
  {
    ViewportResources(legit::Core *core, glm::uvec2 viewportSize, size_t pointsCount)
    {
      this->viewportSize = viewportSize;
      this->mipsCount = 0;
      this->totalBucketsCount = 0;
      std::vector<MipInfo> mipInfosData;
      for (
        glm::uvec2 currMipSize = viewportSize;
        currMipSize.x > 0 && currMipSize.y > 0;
        currMipSize.x /= 2, currMipSize.y /= 2)
      {
        MipInfo mipInfo;
        mipInfo.size = glm::ivec4(currMipSize.x, currMipSize.y, 0, 0);
        mipInfo.bucketIndexOffset = glm::uint(totalBucketsCount);
        mipInfosData.push_back(mipInfo);

        totalBucketsCount += currMipSize.x * currMipSize.y;
        mipsCount++;
      }
      size_t mipInfosSize = sizeof(MipInfo) * mipInfosData.size();
      mipInfosBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), mipInfosSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal));
      legit::LoadBufferData(core, mipInfosData.data(), mipInfosSize, mipInfosBuffer.get());

      this->bucketGroupsCount = 50;
      this->bucketGroupsProxy = core->GetRenderGraph()->AddBuffer<BucketGroup>(uint32_t(bucketGroupsCount));
      this->groupEntriesPoolProxy = core->GetRenderGraph()->AddBuffer<uint32_t>(uint32_t(totalBucketsCount));

      this->maxIndicesCount = pointsCount * 4 + totalBucketsCount;
      this->bucketsProxy = core->GetRenderGraph()->AddBuffer<Bucket>(uint32_t(totalBucketsCount));
      this->bucketEntriesPoolProxy = core->GetRenderGraph()->AddBuffer<BucketEntry>(uint32_t(maxIndicesCount));
      this->mipInfosProxy = core->GetRenderGraph()->AddExternalBuffer(mipInfosBuffer.get());
    }

    //UnmippedProxy tmpBucketTexture;
    legit::RenderGraph::BufferProxyUnique bucketEntriesPoolProxy;
    legit::RenderGraph::BufferProxyUnique bucketsProxy;
    legit::RenderGraph::BufferProxyUnique mipInfosProxy;

    std::unique_ptr<legit::Buffer> mipInfosBuffer;


    legit::RenderGraph::BufferProxyUnique bucketGroupsProxy;
    legit::RenderGraph::BufferProxyUnique groupEntriesPoolProxy;

    size_t totalBucketsCount;
    size_t mipsCount;
    size_t maxIndicesCount;
    size_t bucketGroupsCount;

    glm::uvec2 viewportSize;
  };
  std::unique_ptr<ViewportResources> viewportResources;


  #pragma pack(push, 1)
  struct PassData
  {
    glm::mat4 viewMatrix; //world -> camera
    glm::mat4 projMatrix; //camera -> ndc
    glm::vec4 sortDir;
    glm::uint mipsCount;
    glm::uint totalBucketsCount;
    glm::uint bucketGroupsCount;

    glm::uint maxSizePow;
    glm::uint blockSizePow;
    glm::uint isFirstBlock;

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

  #pragma pack(push, 1)
  struct BucketEntry
  {
    glm::uint pointIndex; 
    glm::uint bucketIndex;
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct BucketGroup
  {
    glm::uint bucketsCount;
    glm::uint bucketIndexOffset;
    glm::uint bucketIndexGlobalOffset;
    float padding;
  };
  #pragma pack(pop)

  struct PointBucketsShaders
  {
    struct ClearShader
    {
      std::unique_ptr<legit::Shader> compute;
    } clearShader;

    struct AllocShader
    {
      std::unique_ptr<legit::Shader> compute;
    } allocShader;

    struct CountShader
    {
      std::unique_ptr<legit::Shader> vertex;
      std::unique_ptr<legit::Shader> fragment;
      std::unique_ptr<legit::ShaderProgram> program;
    } countShader;
    struct FillShader
    {
      std::unique_ptr<legit::Shader> vertex;
      std::unique_ptr<legit::Shader> fragment;
      std::unique_ptr<legit::ShaderProgram> program;
    } fillShader;
  }pointBuckets;

  struct BucketGroupsShaders
  {
    struct ClearShader
    {
      std::unique_ptr<legit::Shader> compute;
    } clearShader;

    struct AllocShader
    {
      std::unique_ptr<legit::Shader> compute;
    } allocShader;

    struct CountShader
    {
      std::unique_ptr<legit::Shader> compute;
    } countShader;
    struct FillShader
    {
      std::unique_ptr<legit::Shader> compute;
    } fillShader;
  }bucketGroups;

  struct SortShader
  {
    std::unique_ptr<legit::Shader> compute;
  } sortShader;

  struct BitonicKernelShader
  {
    std::unique_ptr<legit::Shader> compute;
  } bitonicKernelShader;

  //vk::Extent2D viewportSize;
  
  std::unique_ptr<legit::Sampler> screenspaceSampler;

  std::default_random_engine eng;
  std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };

  legit::Core *core;
};

/*
//https://rosettacode.org/wiki/Sorting_algorithms/Heapsort

bool Compare(int *data, size_t i, size_t j)
{
  return data[i] < data[j];
}
void SiftDown(int *data, size_t begin, size_t end)
{
  size_t root = begin;

  while (root * 2 + 1 < end)
  {
    size_t child = root * 2 + 1;
    if (child + 1 < end && Compare(data, child, child + 1))
      child = child + 1;
    if (Compare(data, root, child))
    {
      std::swap(data[root], data[child]);
      root = child;
    }
    else
    {
      return;
    }
  }
}

void Heapify(int *data, size_t count)
{
  int begin = count > 1 ? ((count - 1) / 2) : 0;

  while (begin >= 0)
  {
    SiftDown(data, begin, count);
    begin--;
  }
}

void HeapSort(int *data, size_t count)
{
  Heapify(data, count);

  size_t end = count;
  while (end > 1)
  {
    std::swap(data[end - 1], data[0]);
    end--;
    SiftDown(data, 0, end);
  }
}

void BitonicKernel(int *data, size_t size, size_t sortGroupSize, size_t blockSize)
{
  for (size_t i = 0; i < size; i++)
  {
    bool sortDir = ((i & sortGroupSize) == 0);
    size_t j = (i | blockSize);
    if ((i & blockSize) == 0 && j < size)
    {
      if (Compare(data, i, j) == sortDir)
        std::swap(data[i], data[j]);
    }
  }
}

void BitonicSort(int *data, size_t size)
{
  for (size_t sortGroupSize = 2; sortGroupSize <= size; sortGroupSize *= 2)
  {
    for (size_t blockSize = sortGroupSize >> 1; blockSize > 0; blockSize = blockSize >> 1)
    {
      BitonicKernel(data, size, sortGroupSize, blockSize);
    }
  }
}

struct Pair
{
  size_t nodeIndices[2];
};

//https://upload.wikimedia.org/wikipedia/commons/thumb/c/c6/BitonicSort.svg/843px-BitonicSort.svg.png
Pair GetBitonicPair(size_t pairIndex, glm::uint blockSizePow, bool isFirstBlock)
{
  size_t blockSize = 1ull << blockSizePow;
  //size_t blockIndex = pairIndex / (blockSize / 2);
  size_t blockIndex = pairIndex >> (blockSizePow - 1);
  size_t blockOffset = blockSize * blockIndex;

  //size_t blockLocalIndex0 = pairIndex % (blockSize / 2);
  size_t blockLocalIndex0 = pairIndex & ((blockSize >> 1) - 1);
  size_t blockLocalIndex1 = isFirstBlock ? (blockSize - 1 - blockLocalIndex0) : (blockLocalIndex0 + blockSize / 2);
  return Pair{ blockOffset + blockLocalIndex0, blockOffset + blockLocalIndex1 };
}



struct Point
{
  float val;
  size_t bucketId;
};
void BitonicSort2(Point *points, size_t size)
{
  glm::uint maxBlockPow = GetMaxPow(size);
  for (glm::uint maxSizePow = 1; maxSizePow <= maxBlockPow; maxSizePow++)
  {
    bool isFirstBlock = true;
    for (glm::uint blockSizePow = maxSizePow; blockSizePow > 0; blockSizePow--)
    {
      for (size_t pairIndex = 0; pairIndex < (1 << maxBlockPow); pairIndex++)
      {
        Pair pair = GetBitonicPair(pairIndex, blockSizePow, isFirstBlock);
        if (
          pair.nodeIndices[1] < size && 
          points[pair.nodeIndices[0]].bucketId == points[pair.nodeIndices[1]].bucketId &&
          points[pair.nodeIndices[0]].val > points[pair.nodeIndices[1]].val)
          std::swap(points[pair.nodeIndices[0]], points[pair.nodeIndices[1]]);
      }
      isFirstBlock = false;
    }
  }
}

void TestSort()
{
  std::default_random_engine eng;
  std::uniform_int_distribution<int> dis(0, 100);

  size_t pointsCount = 300;
  size_t trialsCount = 100000;

  std::vector<Point> points;
  size_t bucketId = 0;
  for (size_t i = 0; i < pointsCount; i++)
  {
    Point point;
    point.val = dis(eng);
    point.bucketId = bucketId;
    points.push_back(point);
    if (dis(eng) > 80)
      bucketId++;
  }
  BitonicSort2(points.data(), points.size());
  //HeapSort(points.data(), points.size());
  int p = 1;
}
*/


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