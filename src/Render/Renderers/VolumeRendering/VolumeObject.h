
template<typename TexelType>
TexelType *GetClampedTexel(void *data, glm::ivec3 pos, glm::uvec3 size)
{
  pos.x = std::min(pos.x, glm::i32(size.x) - 1);
  pos.y = std::min(pos.y, glm::i32(size.y) - 1);
  pos.z = std::min(pos.z, glm::i32(size.z) - 1);
  pos.x = std::max(pos.x, 0);
  pos.y = std::max(pos.y, 0);
  pos.z = std::max(pos.z, 0);
  size_t offset = (pos.x + pos.y * size.x + pos.z * size.x * size.y) * sizeof(TexelType);
  return (TexelType*)((char *)data + offset);

};
template<typename ChannelType>
void CalculateNormals(void *srcData, void *dstData, glm::uvec3 size)
{

  struct SrcTexel
  {
    ChannelType density;
  };
  struct DstTexel
  {
    ChannelType density;
    ChannelType gradient[3];
  };

  glm::ivec3 node;
  for (node.z = 0; node.z < int(size.z); node.z++)
  {
    for (node.y = 0; node.y < int(size.y); node.y++)
    {
      for (node.x = 0; node.x < int(size.x); node.x++)
      {
        SrcTexel *centerTexel = GetClampedTexel<SrcTexel>(srcData, node, size);
        DstTexel *dstTexel = GetClampedTexel<DstTexel>(dstData, node, size);
        dstTexel->density = centerTexel->density;

        /*SrcTexel *xOffsetTexels[2] = { GetClampedTexel<SrcTexel>(srcData, node + glm::ivec3(-1, 0, 0), size), GetClampedTexel<SrcTexel>(srcData, node + glm::ivec3(1, 0, 0), size) };
        SrcTexel *yOffsetTexels[2] = { GetClampedTexel<SrcTexel>(srcData, node + glm::ivec3(0, -1, 0), size), GetClampedTexel<SrcTexel>(srcData, node + glm::ivec3(0, 1, 0), size) };
        SrcTexel *zOffsetTexels[2] = { GetClampedTexel<SrcTexel>(srcData, node + glm::ivec3(0, 0, -1), size), GetClampedTexel<SrcTexel>(srcData, node + glm::ivec3(0, 0, 1), size) };
        glm::ivec3 gradient = {
        xOffsetTexels[1]->density - xOffsetTexels[0]->density,
        yOffsetTexels[1]->density - yOffsetTexels[0]->density,
        zOffsetTexels[1]->density - zOffsetTexels[0]->density };*/
        glm::ivec3 gradient;
        for (size_t axis = 0; axis < 3; axis++)
        {
          glm::ivec3 mainAxis;
          glm::ivec3 sideAxes[2];
          int *dstComponent = nullptr;
          switch (axis)
          {
          case 0:
          {
            mainAxis = glm::ivec3(1, 0, 0);
            sideAxes[0] = glm::ivec3(0, 1, 0);
            sideAxes[1] = glm::ivec3(0, 0, 1);
            dstComponent = &gradient.x;
          }break;
          case 1:
          {
            mainAxis = glm::ivec3(0, 1, 0);
            sideAxes[0] = glm::ivec3(1, 0, 0);
            sideAxes[1] = glm::ivec3(0, 0, 1);
            dstComponent = &gradient.y;
          }break;
          case 2:
          {
            mainAxis = glm::ivec3(0, 0, 1);
            sideAxes[0] = glm::ivec3(1, 0, 0);
            sideAxes[1] = glm::ivec3(0, 1, 0);
            dstComponent = &gradient.z;
          }break;
          }
          *dstComponent = 0;
          int weights[] = { 1, 2, 1, 2, 4, 2, 1, 2, 1 };
          for (int x = 0; x < 3; x++)
          {
            for (int y = 0; y < 3; y++)
            {
              glm::ivec2 offset = { x - 1, y - 1 };
              int weight = weights[x + 3 * y];
              *dstComponent += GetClampedTexel<SrcTexel>(srcData, node + mainAxis + sideAxes[0] * offset.x + sideAxes[1] * offset.y, size)->density * weight;
              *dstComponent -= GetClampedTexel<SrcTexel>(srcData, node - mainAxis + sideAxes[0] * offset.x + sideAxes[1] * offset.y, size)->density * weight;
            }
          }
        }

        int max = std::numeric_limits<ChannelType>::max();

        gradient += glm::ivec3(max) / 2;
        gradient.x = std::max(0, std::min(gradient.x, max));
        gradient.y = std::max(0, std::min(gradient.y, max));
        gradient.z = std::max(0, std::min(gradient.z, max));

        dstTexel->gradient[0] = gradient.x;
        dstTexel->gradient[1] = gradient.y;
        dstTexel->gradient[2] = gradient.z;
      }
    }
  }
}

struct VolumeObject
{
  VolumeObject(std::string metadataFilename)
  {
    Json::Value root;
    Json::Reader reader;

    std::ifstream fileStream(metadataFilename);
    bool result = reader.parse(fileStream, root);

    auto formatNode = root["format"];

    if (formatNode["data"] == "uint8_t")
    {
      this->volumeData.format = vk::Format::eR8Unorm;
      this->volumeData.texelSize = 1;
    }
    else
      if (formatNode["data"] == "uint16_t")
      {
        this->volumeData.format = vk::Format::eR16Unorm;
        this->volumeData.texelSize = 2;
      }
      else
      {
        assert(!!"unknown format");
      }

    std::string datasetFile = root["dataset"].asString();
    std::ifstream datasetStream(datasetFile, std::ios::binary);

    if (formatNode.isMember("resolution"))
    {
      if (formatNode["resolution"] == "uint16_t")
      {
        unsigned short size[3];
        datasetStream.read((char*)size, sizeof(size));
        this->volumeData.baseSize = glm::uvec3(size[0], size[1], size[2]);
      }
    }
    else
      if (root.isMember("resolution"))
      {
        this->volumeData.baseSize = ReadJsonVec3u(root["resolution"]);
      }


    this->minPoint = ReadJsonVec3f(root["volume"]["min"]);
    this->maxPoint = ReadJsonVec3f(root["volume"]["max"]);

    this->volumeData.layersCount = 1;
    size_t mipsCount = 1;
    this->volumeData.layersCount = 1;
    this->volumeData.mips.resize(mipsCount);
    this->volumeData.mips[0].layers.resize(this->volumeData.layersCount);
    this->volumeData.mips[0].layers[0].offset = 0;
    this->volumeData.mips[0].size = this->volumeData.baseSize;


    size_t totalSize = (volumeData.baseSize.x * volumeData.baseSize.y * volumeData.baseSize.z) * volumeData.texelSize;
    this->volumeData.texels.resize(totalSize);

    datasetStream.read((char*)(volumeData.texels.data()), totalSize);
  }

  VolumeObject CalculateNormals()
  {
    VolumeObject res = *this;
    switch (this->volumeData.format)
    {
    case vk::Format::eR8Unorm:
    {
      res.volumeData.format = vk::Format::eR8G8B8A8Unorm;
      res.volumeData.texelSize = 4 * sizeof(uint8_t);
      res.volumeData.mips[0].layers[0].offset = 0;
      res.volumeData.mips[0].size = res.volumeData.baseSize;
      size_t totalSize = (res.volumeData.baseSize.x * res.volumeData.baseSize.y * res.volumeData.baseSize.z) * res.volumeData.texelSize;
      res.volumeData.texels.resize(totalSize);

      ::CalculateNormals<uint8_t>(this->volumeData.texels.data(), res.volumeData.texels.data(), res.volumeData.baseSize);

    }break;
    case vk::Format::eR16Unorm:
    {
      res.volumeData.format = vk::Format::eR16G16B16A16Unorm;
      res.volumeData.texelSize = 4 * sizeof(uint16_t);
      res.volumeData.mips[0].layers[0].offset = 0;
      res.volumeData.mips[0].size = res.volumeData.baseSize;
      size_t totalSize = (res.volumeData.baseSize.x * res.volumeData.baseSize.y * res.volumeData.baseSize.z) * res.volumeData.texelSize;
      res.volumeData.texels.resize(totalSize);
      ::CalculateNormals<uint16_t>(this->volumeData.texels.data(), res.volumeData.texels.data(), res.volumeData.baseSize);
    }break;
    }
    return res;
  }


  glm::vec3 minPoint, maxPoint;
  legit::ImageTexelData volumeData;
};
