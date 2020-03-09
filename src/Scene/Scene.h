struct Object
{
  Object()
  {
    mesh = nullptr;
    objToWorld = glm::mat4();
    albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    emissiveColor = glm::vec3(1.0f, 1.0f, 1.0f);
    isShadowReceiver = true;
  }

  Mesh *mesh;
  glm::mat4 objToWorld;

  glm::vec3 albedoColor;
  glm::vec3 emissiveColor;
  bool isShadowReceiver;
};

struct Camera
{
  Camera()
  {
    pos = glm::vec3(0.0f);
    vertAngle = 0.0f;
    horAngle = 0.0f;
  }
  glm::vec3 pos;
  float vertAngle, horAngle;
  glm::mat4 GetTransformMatrix() const
  {
    return glm::translate(pos) * glm::rotate(horAngle, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::rotate(vertAngle, glm::vec3(1.0f, 0.0f, 0.0f));
  }
};

class Scene
{
public:
  enum struct GeometryTypes
  {
    Triangles,
    RegularPoints,
    SizedPoints
  };
  Scene(Json::Value sceneConfig, legit::Core *core, GeometryTypes geometryType)
  {
    this->core = core;

    vertexDecl = Mesh::GetVertexDeclaration();
    legit::ExecuteOnceQueue transferQueue(core);

    std::map<std::string, Mesh*> nameToMesh;

    auto transferCommandBuffer = transferQueue.BeginCommandBuffer();
    {
      Json::Value meshArray = sceneConfig["meshes"];
      for (Json::ArrayIndex meshIndex = 0; meshIndex < meshArray.size(); meshIndex++)
      {
        Json::Value currMeshNode = meshArray[meshIndex];

        std::string meshFilename = currMeshNode.get("filename", "<unspecified>").asString();
        glm::vec3 scale = ReadJsonVec3f(currMeshNode["scale"]);

        auto meshData = MeshData(meshFilename, scale);
        switch (geometryType)
        {
          case GeometryTypes::RegularPoints:
          {
            float splatSize = 0.1f;
            meshData = MeshData::GeneratePointMeshRegular(meshData, std::pow(1.0f / splatSize, 2));
          }break;
          case GeometryTypes::SizedPoints:
          {
            meshData = MeshData::GeneratePointMeshSized(meshData, 1);
          }break;
        }
        auto mesh = std::unique_ptr<Mesh>(new Mesh(meshData, core->GetPhysicalDevice(), core->GetLogicalDevice(), transferCommandBuffer));
        meshes.push_back(std::move(mesh));

        std::string meshName = currMeshNode.get("name", "<unspecified>").asString();
        nameToMesh[meshName] = meshes.back().get();
      }
    }
    transferQueue.EndCommandBuffer();


    for (Json::ArrayIndex objectIndex = 0; objectIndex < sceneConfig["objects"].size(); objectIndex++)
    {
      Object object;

      Json::Value currObjectNode = sceneConfig["objects"][objectIndex];
      std::string meshName = currObjectNode.get("mesh", "<unspecified>").asString();
      if (nameToMesh.find(meshName) == nameToMesh.end())
      {
        std::cout << "Mesh " << meshName << " not specified";
        continue;
      }

      object.mesh = nameToMesh[meshName];
      glm::vec3 rotationVec = ReadJsonVec3f(currObjectNode["angle"]);
      object.objToWorld = glm::translate(ReadJsonVec3f(currObjectNode["pos"]));
      if(glm::length(rotationVec) > 1e-3f)
        object.objToWorld = object.objToWorld * glm::rotate(glm::length(rotationVec), glm::normalize(rotationVec));


      object.albedoColor = ReadJsonVec3f(currObjectNode["albedoColor"]);
      object.emissiveColor = ReadJsonVec3f(currObjectNode["emissiveColor"]);
      object.isShadowReceiver = currObjectNode.get("isShadowCaster", true).asBool();
      
      if (currObjectNode.get("isMarker", false).asBool())
      {
        markerObjectIndex = objects.size();
      }

      objects.push_back(object);
    }

    /*std::unique_ptr<Mesh> sponzaMesh;
    auto transferCommandBuffer = transferQueue.BeginCommandBuffer();
    {
      sponzaMesh.reset(new Mesh(core.get(), transferCommandBuffer));
    }
    transferQueue.EndCommandBuffer();

    std::vector<Object> objects;
    for (size_t x = 0; x < 10; x++)
    {
      for (size_t y = 0; y < 10; y++)
      {
        Object object;
        object.mesh = sponzaMesh.get();
        object.transform = glm::translate(glm::vec3(x * 1.3f, 0.0f, y * 1.3f)) * glm::rotate(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));// *glm::scale(glm::vec3(0.01f, 0.01f, 0.01f));
        objects.push_back(object);
      }
    }
    */
  }

  using ObjectCallback = std::function<void(glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer, vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)>;
  void IterateObjects(ObjectCallback objectCallback)
  {
    for (auto &object : objects)
    {
      objectCallback(object.objToWorld, object.albedoColor, object.emissiveColor, object.mesh->vertexBuffer->GetBuffer(), object.mesh->indexBuffer ? object.mesh->indexBuffer->GetBuffer() : nullptr, uint32_t(object.mesh->verticesCount), uint32_t(object.mesh->indicesCount));
    }
  }
private:
  std::vector<std::unique_ptr<Mesh>> meshes;
  std::vector<Object> objects;
  size_t markerObjectIndex;

  legit::VertexDeclaration vertexDecl;
  legit::Core *core;
};