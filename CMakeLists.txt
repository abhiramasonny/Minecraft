cmake_minimum_required(VERSION 3.16)
project(renderer LANGUAGES CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJC_STANDARD 11)
set(CMAKE_OBJCXX_STANDARD 17)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.3.9
)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

add_executable(renderer
  src/app/main.cpp
  src/app/save.cpp
  src/core/math.cpp
  src/core/perlin.cpp
  src/game/player.cpp
  src/game/world.cpp
  src/game/lighting.cpp
  src/game/inventory.cpp
  src/game/ui.cpp
  src/game/entity.cpp
  src/render/textures.cpp
  src/render/renderer.cpp
  src/render/metal_raytracer.mm
  src/third_party/stb_image.cpp
)

target_link_libraries(renderer PRIVATE glfw OpenGL::GL)
target_include_directories(renderer PRIVATE src)

if(APPLE)
  target_compile_definitions(renderer PRIVATE GL_SILENCE_DEPRECATION)
  
  # Metal and Metal Performance Shaders frameworks
  find_library(METAL_LIBRARY Metal)
  find_library(MPS_LIBRARY MetalPerformanceShaders)
  find_library(FOUNDATION_LIBRARY Foundation)
  
  target_link_libraries(renderer PRIVATE 
    ${METAL_LIBRARY}
    ${MPS_LIBRARY}
    ${FOUNDATION_LIBRARY}
  )
  
  # Set Metal source file to compile as Objective-C++
  set_source_files_properties(
    src/render/metal_raytracer.mm
    PROPERTIES
    LANGUAGE OBJCXX
    COMPILE_FLAGS "-fno-objc-arc"
  )
endif()
