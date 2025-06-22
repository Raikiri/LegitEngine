cmake_minimum_required(VERSION 3.29.0)
project(LegitEngine VERSION 1.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

find_package(Vulkan)
message(STATUS "Vulkan found: ${Vulkan_FOUND}")
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")

set(TINYOBJ_INCLUDE_DIR ./dependencies/tiny_obj_loader/include)
set(SPIRVCROSS_INCLUDE_DIR ./dependencies/spirv-cross)
set(GLI_INCLUDE_DIR ./dependencies/gli)
set(GLM_INCLUDE_DIR ./dependencies/glm/include)
set(JSONCPP_INCLUDE_DIR ./dependencies/jsoncpp)
set(IMGUI_INCLUDE_DIR ./dependencies/imgui)
set(LEGIT_VULKAN_INCLUDE_DIR ./dependencies/LegitVulkan)


find_package(GLFW3)
if(${GLFW3_FOUND})
  message(STATUS "GLFW found: ${GLFW3_FOUND}")
  message(STATUS "GLFW include dir: ${GLFW3_INCLUDE_DIR}")
  message(STATUS "GLFW librarie: ${GLFW3_LIBRARY}")
else()
  message(STATUS "GLFW not found. We'll try to build it, but on linux it requires a bunch of wayland/x11 dependencies")

  #set(GLFW_BUILD_WAYLAND ON)
  #set(GLFW_BUILD_X11 OFF)
  set(GLFW_BUILD_DOCS OFF)
  set(GLFW_BUILD_TESTS OFF)
  set(GLFW_BUILD_EXAMPLES OFF)
  set(GLFW_INSTALL OFF)
  add_subdirectory(./dependencies/glfw)
endif()

include_directories ("${Vulkan_INCLUDE_DIRS}")
include_directories ("${GLM_INCLUDE_DIR}")
include_directories ("${SPIRVCROSS_INCLUDE_DIR}")
include_directories ("${GLI_INCLUDE_DIR}")
include_directories ("${TINYOBJ_INCLUDE_DIR}")
include_directories ("${JSONCPP_INCLUDE_DIR}")
include_directories ("${IMGUI_INCLUDE_DIR}")
include_directories ("${LEGIT_VULKAN_INCLUDE_DIR}")


file(GLOB_RECURSE dep_sources ${SPIRVCROSS_INCLUDE_DIR}/*.cpp ${TINYOBJ_INCLUDE_DIR}/*.cc ${JSONCPP_INCLUDE_DIR}/*.cpp ${IMGUI_INCLUDE_DIR}/*.cpp)
file(GLOB_RECURSE legit_sources ./src/*.cpp)
file(GLOB_RECURSE legit_headers ./src/*.h)

add_executable(${PROJECT_NAME})

# Looks like without this the compiler still has too much optimization enabled in the debug build which makes it impossible to debug optimized code
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:Debug>:-O0>
)

target_sources(${PROJECT_NAME} PRIVATE ${dep_sources} ${legit_sources} ${legit_headers})
target_compile_features(${PROJECT_NAME}  PRIVATE cxx_std_17) #entt requires C++17
target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan glfw)
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin/cmaked")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/cmake")