

with import <nixpkgs> {};

mkShell {
  name = "Legit shell";
  packages = [
    clang
    lldb
    powershell
    glslang
    shaderc
    renderdoc
    tracy
    vulkan-validation-layers
    vulkan-tools-lunarg
    #vulkan-tools        # vulkaninfo
  ];

  buildInputs = with pkgs; [
    cmake
    ninja
    
    #glfw

    vulkan-headers
    vulkan-loader

    wayland
    wayland-protocols
    wayland-scanner
    
    xorg.libX11
    xorg.libXcursor
    xorg.libXrandr
    xorg.libXinerama
    xorg.libXi

    libxkbcommon

    pkg-config
    #autoPatchelfHook
  ];
  nativeBuildInputs = [
  ];

  
  LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [
    pkgs.libxkbcommon
    pkgs.vulkan-loader
    pkgs.vulkan-headers
    pkgs.vulkan-validation-layers
    pkgs.wayland
  ];

  shellHook = ''
    echo Library path: $LD_LIBRARY_PATH
  '';
}