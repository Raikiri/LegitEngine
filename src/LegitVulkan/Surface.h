namespace legit
{
  struct WindowDesc
  {
    HINSTANCE hInstance;
    HWND hWnd;
  };

  static vk::UniqueSurfaceKHR CreateWin32Surface(vk::Instance instance, WindowDesc desc)
  {
    auto surfaceCreateInfo = vk::Win32SurfaceCreateInfoKHR()
      .setHwnd(desc.hWnd)
      .setHinstance(desc.hInstance);

    return instance.createWin32SurfaceKHRUnique(surfaceCreateInfo);

  }
}