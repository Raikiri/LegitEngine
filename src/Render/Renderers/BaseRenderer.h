#include "../Common/MipBuilder.h"
#include "../Common/BlurBuilder.h"
#include "../Common/DebugRenderer.h"
#include "../Common/InterleaveBuilder.h"

class BaseRenderer
{
public:
  BaseRenderer(){}
  virtual ~BaseRenderer() {}
  virtual void RecreateSceneResources(Scene *scene){}
  virtual void RecreateSwapchainResources(vk::Extent2D viewportExtent, size_t inFlightFramesCount){}
  virtual void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, const Camera &camera, const Camera &light, Scene *scene, GLFWwindow *window){}
  virtual void ReloadShaders(){}
  virtual void ChangeView(){}
};