namespace legit
{
  class Sampler
  {
  public:
    vk::Sampler GetHandle() const
    {
      return samplerHandle.get();
    }
    
    bool operator <(const Sampler &other) const
    {
      return std::tie(samplerHandle.get()) < std::tie(other.samplerHandle.get());
    }
    Sampler(vk::Device logicalDevice, vk::SamplerAddressMode addressMode, vk::Filter minMagFilterType, vk::SamplerMipmapMode mipFilterType, bool useComparison = false, vk::BorderColor borderColor = vk::BorderColor())
    {
      auto samplerCreateInfo = vk::SamplerCreateInfo()
        .setAddressModeU(addressMode)
        .setAddressModeV(addressMode)
        .setAddressModeW(addressMode)
        .setAnisotropyEnable(false)
        .setCompareEnable(useComparison)
        .setCompareOp(useComparison ? vk::CompareOp::eLessOrEqual : vk::CompareOp::eAlways)
        .setMagFilter(minMagFilterType)
        .setMinFilter(minMagFilterType)
        .setMaxLod(1e7f)
        .setMinLod(0.0f)
        .setMipmapMode(mipFilterType)
        .setUnnormalizedCoordinates(false)
        .setBorderColor(borderColor);

      samplerHandle = logicalDevice.createSamplerUnique(samplerCreateInfo);
    }
  private:
    vk::UniqueSampler samplerHandle;
  };
}