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
  src/ConsoleRenderer.cpp
  src/Sampler.cpp
  src/ConfigLoader.cpp
  src/EnvConfig.cpp
  src/EventBus.h
  src/EnergySystem.cpp
  src/EnergySystem.h
  src/ParamSchema.cpp
  src/ParamSchema.h
  src/LauncherApp.cpp
  src/LauncherApp.h
  src/WinFs.cpp
  src/WinFs.h
  src/HttpServer.cpp
  src/HttpServer.h
  src/WebRenderer.cpp
  src/WebRenderer.h
  src/mod_api.h
  src/mods_registry.cpp
  src/mods_registry.h
  mods/example_periodic_drain.cpp  # 显式文件列表：新增 mod = 这里加一行
  mods/example_plague_strike.cpp
  src/Vec2.h
  src/BallType.h
  src/Ball.h
  src/NucleusParams.h
  src/Nucleus.h
  src/World.h
  src/SpatialGrid.h
  src/IRenderer.h
  src/ConsoleRenderer.h
  src/Sampler.h
  src/ConfigLoader.h
  src/EnvConfig.h
)

target_include_directories(simulator PRIVATE src)

if(WIN32)
  # 启动器模式：HTTP 服务器（WinSock）+ 浏览器自动打开（ShellExecute）。
  target_link_libraries(simulator PRIVATE ws2_32 shell32)
endif()

if(MSVC)
  target_compile_options(simulator PRIVATE /W4)
else()
  target_compile_options(simulator PRIVATE -Wall -Wextra)
endif()
