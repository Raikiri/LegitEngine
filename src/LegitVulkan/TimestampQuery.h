namespace legit
{
  struct TimestampQuery
  {

    TimestampQuery(vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, uint32_t _maxTimestampsCount)
    {
      auto queryPoolInfo = vk::QueryPoolCreateInfo()
        .setQueryType(vk::QueryType::eTimestamp)
        .setQueryCount(_maxTimestampsCount);
      this->queryPool = logicalDevice.createQueryPoolUnique(queryPoolInfo);
      this->timestampDatum.resize(_maxTimestampsCount);
      this->queryResults.resize(_maxTimestampsCount);
      this->currTimestampIndex = 0;
      this->timestampPeriod = physicalDevice.getProperties().limits.timestampPeriod;
      //this->timestampMask = physicalDevice.getProperties().limits.time
    }
    void ResetQueryPool(vk::CommandBuffer commandBuffer)
    {
      commandBuffer.resetQueryPool(queryPool.get(), 0, uint32_t(timestampDatum.size()));
      currTimestampIndex = 0;
    }
    void AddTimestamp(vk::CommandBuffer commandBuffer, size_t timestampName, vk::PipelineStageFlagBits pipelineStage)
    {
      assert(currTimestampIndex < timestampDatum.size());
      commandBuffer.writeTimestamp(pipelineStage, queryPool.get(), currTimestampIndex);
      timestampDatum[currTimestampIndex].timestampName = timestampName;
      currTimestampIndex++;
    }
    struct QueryResult
    {
      struct TimestampData
      {
        size_t timestampName;
        double time;
      };

      const TimestampData *data;
      size_t size;
    };
    QueryResult QueryResults(vk::Device logicalDevice)
    {
      std::fill(queryResults.begin(), queryResults.end(), 0);
      logicalDevice.getQueryPoolResults(queryPool.get(), 0, currTimestampIndex, queryResults.size() * sizeof(std::uint64_t), (void*)queryResults.data(), sizeof(std::uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
      for (uint32_t timestampIndex = 0; timestampIndex < currTimestampIndex; timestampIndex++)
      {
        timestampDatum[timestampIndex].time = (queryResults[timestampIndex] - queryResults[0]) * double(timestampPeriod / 1e9); //in seconds
      }

      QueryResult res;
      res.data = timestampDatum.data();
      res.size = currTimestampIndex;
      return res;
    }
  private:
    std::vector<uint64_t> queryResults;
    std::vector<QueryResult::TimestampData> timestampDatum;
    vk::UniqueQueryPool queryPool;
    uint32_t currTimestampIndex;
    float timestampPeriod;
    uint64_t timestampMask;
  };
}