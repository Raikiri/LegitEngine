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
  private:
    ShaderModule(vk::Device device, std::string filename)
    {
      std::ifstream file(filename, std::ios::ate | std::ios::binary);

      if (!file.is_open())
        throw std::runtime_error("failed to open file!");

      size_t fileSize = (size_t)file.tellg();
      std::vector<char> bytecode(fileSize);

      file.seekg(0);
      file.read(bytecode.data(), fileSize);
      file.close();

      Init(device, bytecode);
    }
    ShaderModule(vk::Device device, const std::vector<char> &bytecode)
    {
      Init(device, bytecode);
    }
    void Init(vk::Device device, const std::vector<char> &bytecode)
    {
      auto shaderModuleCreateInfo = vk::ShaderModuleCreateInfo()
        .setCodeSize(bytecode.size())
        .setPCode(reinterpret_cast<const uint32_t*>(bytecode.data()));
      this->shaderModule = device.createShaderModuleUnique(shaderModuleCreateInfo);
    }
    vk::UniqueShaderModule shaderModule;
    friend class Core;
  };
}