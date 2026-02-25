//
// Copyright (c) 2025 SICK AG, Waldkirch
//
// SPDX-License-Identifier: MIT

#include "GenIStreamC.h"
using namespace gsc;

#include "GenIStreamCHelpers.hpp"
using namespace gscx;

#include <cstdlib>
#include <iostream>
#include <sstream>

#include "helpers.h"

static int runScanDemo(uint32_t timeout)
{
  // tag::initialize_error_handle[]
  Error error;
  // end::initialize_error_handle[]

  // tag::initialize_library[]
  gsLibraryInit(error.ptr());
  throwOnError(error, "Failed to initialize GenIStreamC library: ");
  // end::initialize_library[]

  // tag::library_cleanup_function[]
  ScopeExit libCloser([=]() { gsLibraryClose(GsIgnoreErrorInfo); });
  // end::library_cleanup_function[]

  // tag::initialize_device_discovery_object[]
  Discovery discovery;
  gsLibraryGetDiscovery(discovery.ptr(), error.ptr());
  throwOnError(error, "Failed to get discovery object: ");
  // end::initialize_device_discovery_object[]

  // tag::device_discovery[]
  gsDiscoveryScanForCameras(discovery, timeout, error.ptr());
  throwOnError(error, "Failed to scan for cameras: ");

  // Get number of discovered cameras
  auto numCameras = gsDiscoveryGetNumDiscoveredCameras(discovery, error.ptr());
  throwOnError(error, "Failed to get number of cameras: ");

  if (numCameras == 0)
  {
    std::cerr << "No GigE Vision devices discovered in this system\n";
    return EXIT_FAILURE;
  }

  // Print number of found devices
  std::cout << "Number of found devices: " << numCameras << "\n\n";
  // end::device_discovery[]

  // tag::print_all_device_info[]
  // Print device info for every found device
  for (size_t i = 0; i < numCameras; ++i)
  {
    DiscoveredCamera discoveredCamera;
    gsDiscoveryGetDiscoveredCamera(discovery, i, discoveredCamera.ptr(), error.ptr());
    throwOnError(error);
    printDeviceInfo(discoveredCamera, error);
  }
  // end::print_all_device_info[]

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
  constexpr unsigned defBroadcastTimeout = 500u;

  unsigned int timeoutMs = defBroadcastTimeout;
  int exitCode = EXIT_FAILURE;
  bool showHelpAndExit = false;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i)
  {
    std::istringstream argstream(argv[i]);

    if (argstream.get() != '-')
    {
      showHelpAndExit = true;
      break;
    }
    switch (argstream.get())
    {
      case 'h':
        showHelpAndExit = true;
        break;
      case 't':
        argstream >> timeoutMs;
        break;
      default:
        showHelpAndExit = true;
        break;
    }
  }

  if (showHelpAndExit)
  {
    std::cout << argv[0] << " [option]*"
              << "\n";
    std::cout << "where option is one of"
              << "\n";
    std::cout << "  -h          show this help and exit"
              << "\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds (default: 500)\n";
    std::cout << "\n";
    std::cout << "This sample scans for available SICK Visionary GigEVision sensors on the network\n";
    std::cout << "and displays information about each discovered device.\n";

    return EXIT_FAILURE;
  }

  // Run the scan demo
  try
  {
    exitCode = runScanDemo(timeoutMs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = EXIT_FAILURE;
  }

  std::cout << "Exit code " << exitCode << "\n";

  return exitCode;
}
