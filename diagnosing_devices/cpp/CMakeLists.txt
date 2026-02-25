#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25...3.31)

message(STATUS "SAMPLE: Diagnosing devices")
project(diagnosing_devices
  VERSION 0.1.0
  DESCRIPTION "Read-out diagnostic data and extract system log using GenIStreamC"
  LANGUAGES CXX C
)

### COMPILER FLAGS ###
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

### Language standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# If the project is not built from the repository root, fetch dependencies via FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(SAMPLES_OUTPUT_DIR "${CMAKE_BINARY_DIR}/output")
  set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.fetchcontent")
  
  # Set output directories for all configurations to prevent VS from creating empty Debug/Release folders
  if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "DEBUG;RELEASE" CACHE STRING "" FORCE)
    foreach(CONFIG_TYPE DEBUG RELEASE)
      set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE} ${SAMPLES_OUTPUT_DIR}/${CONFIG_TYPE})
      set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE} ${SAMPLES_OUTPUT_DIR}/${CONFIG_TYPE})
      set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE} ${SAMPLES_OUTPUT_DIR}/${CONFIG_TYPE})
    endforeach()
  else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${SAMPLES_OUTPUT_DIR})
  endif()

  include(FetchContent)
  FetchContent_Declare(
    sick_visionary_gev_base
    GIT_REPOSITORY https://github.com/SICKAG/sick_visionary_gev_base.git
    GIT_TAG        1.0.0
  )
  FetchContent_MakeAvailable(sick_visionary_gev_base)

  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../shared/cpp ${CMAKE_BINARY_DIR}/shared_cpp)
  SICK_copy_runtime_files(${SAMPLES_OUTPUT_DIR})
endif()

### Executables
add_executable(device_state device_state.cpp)
target_link_libraries(device_state 
    PRIVATE GenIStreamC::GenIStreamC
    PRIVATE shared_cpp_utils)

add_executable(device_state_extended device_state_extended.cpp)
target_link_libraries(device_state_extended 
    PRIVATE GenIStreamC::GenIStreamC
    PRIVATE shared_cpp_utils)

add_executable(system_log system_log.cpp)
target_link_libraries(system_log 
    PRIVATE GenIStreamC::GenIStreamC
    PRIVATE shared_cpp_utils)