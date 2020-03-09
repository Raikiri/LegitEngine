#include <random>
namespace tinyobj
{
  bool operator < (const tinyobj::index_t &left, const tinyobj::index_t &right)
  {
    return std::tie(left.vertex_index, left.normal_index, left.texcoord_index) < std::tie(right.vertex_index, right.normal_index, right.texcoord_index);
  }
}

struct MeshData
{
  MeshData() {}

  MeshData(std::string filename, glm::vec3 scale)
  {
    this->primitiveTopology = vk::PrimitiveTopology::eTriangleList;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;
    //std::string mesh_filename = "../data/Meshes/cube.obj";
    //std::string mesh_filename = "../data/Meshes/crytek-sponza/sponza.obj";
    std::cout << "Loading mesh: " << filename << "\n";
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), nullptr, true);
    std::cout << "Warnings: " << warn << "\n";
    if (!ret)
    {
      std::cout << "Errors: " << err << "\n";
    }
    else
    {
      std::cout << "Mesh loaded\n";
    }

    std::map<tinyobj::index_t, size_t> deduplicatedIndices;
    for (auto &shape : shapes)
    {
      for (auto &index : shape.mesh.indices)
      {
        if (deduplicatedIndices.find(index) == deduplicatedIndices.end())
        {
          deduplicatedIndices[index] = vertices.size();
          Vertex vertex;
          vertex.pos = glm::vec3(attrib.vertices[index.vertex_index * 3 + 0], attrib.vertices[index.vertex_index * 3 + 1], attrib.vertices[index.vertex_index * 3 + 2]) * scale;
          if (index.normal_index != -1)
            vertex.normal = glm::vec3(attrib.normals[index.normal_index * 3 + 0], attrib.normals[index.normal_index * 3 + 1], attrib.normals[index.normal_index * 3 + 2]);
          else
            vertex.normal = glm::vec3(1.0f, 0.0f, 0.0f);
          if (index.texcoord_index != -1)
            vertex.uv = glm::vec2(attrib.texcoords[index.texcoord_index * 2 + 0], attrib.texcoords[index.texcoord_index * 2 + 1]);
          else
            vertex.uv = glm::vec2(0.0f, 0.0f);
          vertices.push_back(vertex);
        }
        indices.push_back(IndexType(deduplicatedIndices[index]));
      }
    }
  }

  static float GetTriangleArea(glm::vec3 points[3])
  {
    return glm::length(glm::cross(points[1] - points[0], points[2] - points[0])) * 0.5f;
  }
  

  static MeshData GeneratePointMesh(MeshData srcMesh, float density)
  {
    assert(srcMesh.primitiveTopology == vk::PrimitiveTopology::eTriangleList);
    IndexType trianglesCount = IndexType(srcMesh.indices.size() / 3);

    std::vector<float> triangleAreas;
    triangleAreas.resize(trianglesCount);

    float totalArea = 0.0f;
    for (size_t triangleIndex = 0; triangleIndex < trianglesCount; triangleIndex++)
    {
      glm::vec3 points[3];
      for (size_t vertexNumber = 0; vertexNumber < 3; vertexNumber++)
        points[vertexNumber] = srcMesh.vertices[srcMesh.indices[triangleIndex * 3 + vertexNumber]].pos;
      float area = GetTriangleArea(points);
      totalArea += area;
      triangleAreas[triangleIndex] = totalArea;
    }

    size_t pointsCount = size_t(totalArea * density);

    static std::default_random_engine eng;
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    MeshData res;
    res.primitiveTopology = vk::PrimitiveTopology::ePointList;
    for (size_t pointIndex = 0; pointIndex < pointsCount; pointIndex++)
    {
      float areaVal = dis(eng);

      auto it = std::lower_bound(triangleAreas.begin(), triangleAreas.end(), areaVal * totalArea);
      if (it == triangleAreas.end())
        continue;

      size_t triangleIndex = it - triangleAreas.begin();

      Vertex triangleVertices[3];
      for (size_t vertexNumber = 0; vertexNumber < 3; vertexNumber++)
        triangleVertices[vertexNumber] = srcMesh.vertices[srcMesh.indices[triangleIndex * 3 + vertexNumber]];

      Vertex vertex = TriangleVertexSample(triangleVertices, glm::vec2(dis(eng), dis(eng)));
      res.vertices.push_back(vertex);
    }
    return res;
  }

  static glm::vec2 HammersleyNorm(glm::uint i, glm::uint N)
  {
    // principle: reverse bit sequence of i
    glm::uint b = (glm::uint(i) << 16u) | (glm::uint(i) >> 16u);
    b = (b & 0x55555555u) << 1u | (b & 0xAAAAAAAAu) >> 1u;
    b = (b & 0x33333333u) << 2u | (b & 0xCCCCCCCCu) >> 2u;
    b = (b & 0x0F0F0F0Fu) << 4u | (b & 0xF0F0F0F0u) >> 4u;
    b = (b & 0x00FF00FFu) << 8u | (b & 0xFF00FF00u) >> 8u;

    return glm::vec2(i, b) / glm::vec2(N, 0xffffffffU);
  }

  static MeshData GeneratePointMeshRegular(MeshData srcMesh, float density)
  {
    assert(srcMesh.primitiveTopology == vk::PrimitiveTopology::eTriangleList);
    IndexType trianglesCount = IndexType(srcMesh.indices.size() / 3);

    std::vector<float> triangleAreas;
    triangleAreas.resize(trianglesCount);

    static std::default_random_engine eng;
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    MeshData res;
    res.primitiveTopology = vk::PrimitiveTopology::ePointList;

    size_t pointsCount = 0;
    float totalArea = 0.0f;
    for (size_t triangleIndex = 0; triangleIndex < trianglesCount; triangleIndex++)
    {
      glm::vec3 points[3];
      for (size_t vertexNumber = 0; vertexNumber < 3; vertexNumber++)
        points[vertexNumber] = srcMesh.vertices[srcMesh.indices[triangleIndex * 3 + vertexNumber]].pos;
      float area = GetTriangleArea(points);
      float pointsCountFloat = area * density;
      glm::uint pointsCount = glm::uint(pointsCountFloat);
      float ratio = pointsCountFloat - float(pointsCount);
      pointsCount += (dis(eng) < ratio) ? 1 : 0;

      Vertex triangleVertices[3];
      for (size_t vertexNumber = 0; vertexNumber < 3; vertexNumber++)
        triangleVertices[vertexNumber] = srcMesh.vertices[srcMesh.indices[triangleIndex * 3 + vertexNumber]];

      for (glm::uint pointNumber = 0; pointNumber < pointsCount; pointNumber++)
      {
        Vertex vertex = TriangleVertexSample(triangleVertices, HammersleyNorm(pointNumber, pointsCount));
        vertex.uv.x = 2.0f / sqrt(density);
        res.vertices.push_back(vertex);
      }
    }
    return res;
  }

  static MeshData GeneratePointMeshSized(MeshData srcMesh, size_t pointsPerTriangleCount)
  {
    static std::default_random_engine eng;
    static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    assert(srcMesh.primitiveTopology == vk::PrimitiveTopology::eTriangleList);
    IndexType trianglesCount = IndexType(srcMesh.indices.size() / 3);

    MeshData res;
    res.primitiveTopology = vk::PrimitiveTopology::ePointList;

    for (size_t triangleIndex = 0; triangleIndex < trianglesCount; triangleIndex++)
    {
      glm::vec3 points[3];
      for (size_t vertexNumber = 0; vertexNumber < 3; vertexNumber++)
        points[vertexNumber] = srcMesh.vertices[srcMesh.indices[triangleIndex * 3 + vertexNumber]].pos;
      float area = GetTriangleArea(points);

      Vertex triangleVertices[3];
      for (size_t vertexNumber = 0; vertexNumber < 3; vertexNumber++)
        triangleVertices[vertexNumber] = srcMesh.vertices[srcMesh.indices[triangleIndex * 3 + vertexNumber]];

      glm::uint resPointsCount = glm::uint(pointsPerTriangleCount);
      float resPointRadius = 2.0f * sqrt(area / pointsPerTriangleCount);
      float maxPointRadius = 0.6f; //0.6f

      if (resPointRadius > maxPointRadius)
      {
        resPointsCount = glm::uint(resPointsCount * std::pow(resPointRadius / maxPointRadius, 2.0) + 0.5f);
        resPointRadius = maxPointRadius;
      }
      for (glm::uint pointNumber = 0; pointNumber < resPointsCount; pointNumber++)
      {
        Vertex vertex = TriangleVertexSample(triangleVertices, /*HammersleyNorm(pointNumber, resPointsCount)*/glm::vec2(dis(eng), dis(eng)));
        vertex.uv.x = resPointRadius;
        res.vertices.push_back(vertex);
      }
    }
    return res;
  }


#pragma pack(push, 1)
  struct Vertex
  {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
  };
#pragma pack(pop)

  static Vertex TriangleVertexSample(Vertex triangleVertices[3], glm::vec2 randVal)
  {
    float sqx = sqrt(randVal.x);
    float y = randVal.y;

    float weights[] = {1.0f - sqx, sqx * (1.0f - y), y * sqx};

    Vertex res = {glm::vec3(0.0f), glm::vec3(0.0f), glm::vec2(0.0f)};
    for (size_t vertexNumber = 0; vertexNumber < 3; vertexNumber++)
    {
      res.pos += triangleVertices[vertexNumber].pos * weights[vertexNumber];
      res.normal += triangleVertices[vertexNumber].normal * weights[vertexNumber];
      res.uv += triangleVertices[vertexNumber].uv * weights[vertexNumber];
    }
    return res;
  }

  using IndexType = uint32_t;
  std::vector<Vertex> vertices;
  std::vector<IndexType> indices;
  vk::PrimitiveTopology primitiveTopology;
};


struct Mesh
{
  Mesh(const MeshData &meshData, vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, vk::CommandBuffer transferCommandBuffer)
  {
    this->primitiveTopology = meshData.primitiveTopology;
    indicesCount = meshData.indices.size();
    verticesCount = meshData.vertices.size();

    vertexBuffer = std::make_unique<legit::StagedBuffer>(physicalDevice, logicalDevice, meshData.vertices.size() * sizeof(MeshData::Vertex), vk::BufferUsageFlagBits::eVertexBuffer);
    if(indicesCount > 0)
      indexBuffer = std::make_unique<legit::StagedBuffer>(physicalDevice, logicalDevice, meshData.indices.size() * sizeof(MeshData::IndexType), vk::BufferUsageFlagBits::eIndexBuffer);

    memcpy(vertexBuffer->Map(), meshData.vertices.data(), sizeof(MeshData::Vertex) * meshData.vertices.size());
    vertexBuffer->Unmap(transferCommandBuffer);

    if (indicesCount > 0)
    {
      memcpy(indexBuffer->Map(), meshData.indices.data(), sizeof(MeshData::IndexType) * meshData.indices.size());
      indexBuffer->Unmap(transferCommandBuffer);
    }
  }
  static legit::VertexDeclaration GetVertexDeclaration()
  {
    legit::VertexDeclaration vertexDecl;
    //interleaved variant
    vertexDecl.AddVertexInputBinding(0, sizeof(MeshData::Vertex));
    vertexDecl.AddVertexAttribute(0, offsetof(MeshData::Vertex, pos), legit::VertexDeclaration::AttribTypes::vec3, 0);
    vertexDecl.AddVertexAttribute(0, offsetof(MeshData::Vertex, normal), legit::VertexDeclaration::AttribTypes::vec3, 1);
    vertexDecl.AddVertexAttribute(0, offsetof(MeshData::Vertex, uv), legit::VertexDeclaration::AttribTypes::vec2, 2);
    //separate buffers variant
    /*vertexDecl.AddVertexInputBinding(0, sizeof(glm::vec3));
    vertexDecl.AddVertexAttribute(0, 0, legit::VertexDeclaration::AttribTypes::vec3, 0);
    vertexDecl.AddVertexInputBinding(1, sizeof(glm::vec3));
    vertexDecl.AddVertexAttribute(1, 0, legit::VertexDeclaration::AttribTypes::vec3, 1);
    vertexDecl.AddVertexInputBinding(2, sizeof(glm::vec2));
    vertexDecl.AddVertexAttribute(2, 0, legit::VertexDeclaration::AttribTypes::vec2, 2);*/

    return vertexDecl;
  }

  std::unique_ptr<legit::StagedBuffer> vertexBuffer;
  std::unique_ptr<legit::StagedBuffer> indexBuffer;
  size_t indicesCount;
  size_t verticesCount;
  vk::PrimitiveTopology primitiveTopology;
};