cmake_minimum_required(VERSION 3.14)
project(AdaptiveOrbitSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(simulator
  src/main.cpp
  src/Ball.cpp
  src/Nucleus.cpp
  src/NucleusParams.cpp
  src/World.cpp
  src/SpatialGrid.cpp
  src/Renderer.cpp
  src/Sampler.cpp
  src/ConfigLoader.cpp
  src/EnvConfig.cpp
  src/Vec2.h
  src/BallType.h
  src/Ball.h
  src/NucleusParams.h
  src/Nucleus.h
  src/World.h
  src/SpatialGrid.h
  src/Renderer.h
  src/Sampler.h
  src/ConfigLoader.h
  src/EnvConfig.h
)

if(MSVC)
  target_compile_options(simulator PRIVATE /W4)
else()
  target_compile_options(simulator PRIVATE -Wall -Wextra)
endif()
