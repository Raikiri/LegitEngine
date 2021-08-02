#include <string>
#include <fstream>
namespace legit
{
  class Core;
  class ShaderModule
  {
  public:
    vk::ShaderModule GetHandle()
    {
      return shaderModule.get();
    }
    ShaderModule(vk::Device device, const std::vector<uint32_t> &bytecode)
    {
      Init(device, bytecode);
    }
  private:
    void Init(vk::Device device, const std::vector<uint32_t> &bytecode)
    {
      auto shaderModuleCreateInfo = vk::ShaderModuleCreateInfo()
        .setCodeSize(bytecode.size() * sizeof(uint32_t))
        .setPCode(bytecode.data());
      this->shaderModule = device.createShaderModuleUnique(shaderModuleCreateInfo);
    }
    vk::UniqueShaderModule shaderModule;
    friend class Core;
  };
}