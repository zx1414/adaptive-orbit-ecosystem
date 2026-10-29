cmake_minimum_required(VERSION 3.16)
project(EcoCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(eco_core STATIC
  src/core/Events.cpp
  src/core/Random.cpp
  src/core/Time.cpp
  src/decision/PolicyRegistry.cpp
  src/decision/RulePolicy.cpp
  src/decision/StubPolicy.cpp
  src/world/Energy.cpp
  src/world/Environment.cpp
  src/world/SpatialIndex.cpp
  src/sim/Simulation.cpp
  src/producers/Producer.cpp
  src/producers/ProducerSystem.cpp
  src/agents/Agent.cpp
  src/agents/AgentSystem.cpp
  src/agents/Behavior.cpp
  src/agents/Sensors.cpp
)
target_include_directories(eco_core PUBLIC src)

if(MSVC)
  target_compile_options(eco_core PUBLIC /W4 /WX /permissive- /utf-8 /fp:precise)
else()
  target_compile_options(eco_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(simulator
  src/app/Cli.cpp
  src/app/ConsoleRenderer.cpp
  src/app/StatsWriter.cpp
  src/app/main.cpp
)
target_link_libraries(simulator PRIVATE eco_core)

add_executable(unit_tests
  tests/unit/main.cpp
  tests/unit/test_energy.cpp
  tests/unit/test_policy.cpp
  tests/unit/test_world.cpp
  tests/unit/test_m25.cpp
)
target_include_directories(unit_tests PRIVATE tests/unit)
target_link_libraries(unit_tests PRIVATE eco_core)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
