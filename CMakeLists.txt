cmake_minimum_required(VERSION 3.24)
project(sigscan LANGUAGES CXX VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SIGSCAN_WITH_OMP  AUTO CACHE STRING "Build OpenMP version (AUTO/ON/OFF)")
set(SIGSCAN_WITH_MPI  AUTO CACHE STRING "Build MPI version (AUTO/ON/OFF)")
set(SIGSCAN_WITH_CUDA AUTO CACHE STRING "Build CUDA version (AUTO/ON/OFF)")
option(SIGSCAN_BUILD_TESTS "Build tests" ON)
option(SIGSCAN_NATIVE "Optimize for the build machine (-mcpu/-march=native)" ON)

# Apple clang has no OpenMP, use libomp from Homebrew
if(APPLE AND NOT OpenMP_ROOT)
  foreach(p /opt/homebrew/opt/libomp /usr/local/opt/libomp)
    if(EXISTS ${p})
      set(OpenMP_ROOT ${p})
    endif()
  endforeach()
endif()

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W3> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>)
endif()
if(SIGSCAN_NATIVE AND NOT MSVC)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-mcpu=native>)
  else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=native>)
  endif()
endif()

# core library used by all programs
add_library(sigscan_core
  src/common/patterns.cpp
  src/common/automaton.cpp
  src/common/io.cpp
  src/common/cli.cpp
  src/common/report.cpp
  src/common/generator.cpp
)
target_include_directories(sigscan_core PUBLIC include)

add_executable(sigscan-gen src/gen/main.cpp)
target_link_libraries(sigscan-gen PRIVATE sigscan_core)

add_executable(sigscan-seq src/seq/main.cpp)
target_link_libraries(sigscan-seq PRIVATE sigscan_core)

# OpenMP
if(NOT SIGSCAN_WITH_OMP STREQUAL "OFF")
  if(SIGSCAN_WITH_OMP STREQUAL "ON")
    find_package(OpenMP REQUIRED COMPONENTS CXX)
  else()
    find_package(OpenMP COMPONENTS CXX)
  endif()
  if(OpenMP_CXX_FOUND)
    add_executable(sigscan-omp src/omp/main.cpp)
    target_link_libraries(sigscan-omp PRIVATE sigscan_core OpenMP::OpenMP_CXX)
  endif()
endif()

# MPI
if(NOT SIGSCAN_WITH_MPI STREQUAL "OFF")
  if(SIGSCAN_WITH_MPI STREQUAL "ON")
    find_package(MPI REQUIRED COMPONENTS CXX)
  else()
    find_package(MPI COMPONENTS CXX)
  endif()
  if(MPI_CXX_FOUND)
    add_executable(sigscan-mpi src/mpi/main.cpp)
    target_link_libraries(sigscan-mpi PRIVATE sigscan_core MPI::MPI_CXX)
  endif()
endif()

# CUDA
if(NOT SIGSCAN_WITH_CUDA STREQUAL "OFF")
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
      set(CMAKE_CUDA_ARCHITECTURES 75)  # Tesla T4
    endif()
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    add_executable(sigscan-cuda src/cuda/main.cu)
    target_link_libraries(sigscan-cuda PRIVATE sigscan_core)
    target_compile_options(sigscan-cuda PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-lineinfo --expt-relaxed-constexpr>)
  elseif(SIGSCAN_WITH_CUDA STREQUAL "ON")
    message(FATAL_ERROR "CUDA requested but no CUDA compiler found")
  endif()
endif()

if(SIGSCAN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "sigscan: OpenMP=${OpenMP_CXX_FOUND} MPI=${MPI_CXX_FOUND} CUDA=${CMAKE_CUDA_COMPILER}")
