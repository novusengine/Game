cmake_minimum_required(VERSION 3.12)

if (POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()

project(Game)

include(CMake/Options.cmake)
include(CMake/Configuration.cmake)
include(CMake/VSFilterUtils.cmake)

add_subdirectory(Dependencies)
add_subdirectory(Submodules)
include(CMake/TreatWarningsAsError.cmake)
add_subdirectory(Source)