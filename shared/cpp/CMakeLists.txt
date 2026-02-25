#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25...3.31)

message(STATUS "Shared C++ helper functions")
project(shared_cpp_utils
        VERSION 0.1.0
        DESCRIPTION "Shared helper functions used in the GEV Samples"
        LANGUAGES CXX C)

### Build type default for single-config generators
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
  SICK_copy_runtime_files(${SAMPLES_OUTPUT_DIR})
endif()

### Windows: only relevant for SHARED libs
if(WIN32)
  # This option only affects SHARED libraries; harmless but not needed for STATIC.
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

### Sources
set(SOURCES
  helpers.cpp
  3pp/simplebmp.cpp
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set_source_files_properties(3pp/simplebmp.cpp PROPERTIES COMPILE_FLAGS "-Wno-unused-result")
endif()

### Library
add_library(${PROJECT_NAME} ${SOURCES})

# If headers are in the current directory:
target_include_directories(${PROJECT_NAME}
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3pp
)

# Propagate the SDK dependency to consumers:
target_link_libraries(${PROJECT_NAME}
  PUBLIC
    GenIStreamC::GenIStreamC
)
