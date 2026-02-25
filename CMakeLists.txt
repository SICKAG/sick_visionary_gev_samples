#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25...3.31)
project(sick_visionary_gev_samples
        VERSION 2.0.0
        DESCRIPTION "samples for Visionary devices"
        HOMEPAGE_URL "https://github.com/SICKAG/sick_visionary_gev_samples"
        LANGUAGES CXX)

# The following path specifies where the executable and shared libraries will be located after building.
# Default inside the build tree.
set(SAMPLES_OUTPUT_DIR "${CMAKE_BINARY_DIR}/output")

# For easier integration in tools like vscode
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/.fetchcontent)

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

# FetchContent: dependencies are fetched from a public github repository.
# The repository contains the GenIStreamC API, as well as required shared library dependencies.
# usage is restricted according to EULA see sick_visionary_gev_base
include(FetchContent)
FetchContent_Declare(
        sick_visionary_gev_base
        GIT_REPOSITORY https://github.com/SICKAG/sick_visionary_gev_base.git
        GIT_TAG        1.0.0
)
FetchContent_MakeAvailable(sick_visionary_gev_base)

# Helper function that copies the platform- and configuration-specific SICK runtime files
# into the given build/output directory. The function should be called
# only once, as it defines the 'sick_copy_runtime' custom target.
SICK_copy_runtime_files(${SAMPLES_OUTPUT_DIR})

add_subdirectory(shared/cpp)
add_subdirectory(tutorial/cpp)
add_subdirectory(imu_data_access/cpp)
add_subdirectory(finding_and_connecting_devices/cpp)
add_subdirectory(diagnosing_devices/cpp)
add_subdirectory(image_streaming_and_storing/cpp)
add_subdirectory(image_frontend_configuration/cpp)