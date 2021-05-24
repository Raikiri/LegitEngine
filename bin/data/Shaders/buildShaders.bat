@echo off
setlocal EnableExtensions EnableDelayedExpansion
set CompilerExe="%VULKAN_SDK%\Bin\glslangValidator.exe"
set OptimizerExe="%VULKAN_SDK%\Bin\spirv-opt.exe"
set OptimizerConfig="OptimizerConfig.cfg"
for /r glsl/ %%I in (*.vert) do (
  set outname=%%I
  set outname=!outname:\glsl\=\spirv\!
  @echo Building %%I
  @echo To !outname!
  %CompilerExe% -V "%%I" -l --target-env vulkan1.1 -o "!outname!".spv
  rem %OptimizerExe% "!outname!"_u.spv -Oconfig="OptimizerConfig.cfg" -o "!outname!".spv
)

for /r glsl/ %%I in (*.frag) do (
  set outname=%%I
  set outname=!outname:\glsl\=\spirv\!
  @echo Building %%I
  @echo To !outname!
  %CompilerExe% -V "%%I" -l --target-env vulkan1.1 -o "!outname!".spv
  rem %OptimizerExe% "!outname!"_u.spv -Oconfig="OptimizerConfig.cfg" -o "!outname!".spv
)

for /r glsl/ %%I in (*.comp) do (
  set outname=%%I
  set outname=!outname:\glsl\=\spirv\!
  @echo Building %%I
  @echo To !outname!
  %CompilerExe% -V "%%I" -l --target-env vulkan1.1 -o "!outname!".spv
  rem %OptimizerExe% "!outname!"_u.spv -Oconfig="OptimizerConfig.cfg" -o "!outname!".spv
)
rem pause

rem 
rem %CompilerExe% -V glsl/fragmentShader.frag -o spirv/fragmentShader.spv
rem %CompilerExe% -V glsl/frameDescriptorLayout.comp -o spirv/frameDescriptorLayout.spv
rem %CompilerExe% -V glsl/passDescriptorLayout.comp -o spirv/passDescriptorLayout.spv
