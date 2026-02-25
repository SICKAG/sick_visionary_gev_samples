//
// Copyright (c) 2025 SICK AG, Waldkirch
//
// SPDX-License-Identifier: MIT

#include "GenIStreamC.h"
using namespace gsc;

#include "GenIStreamCHelpers.hpp"
using namespace gscx;

#include <iostream>
#include <sstream>

#include "helpers.h"

static int runDeviceState(const std::string& serialNum, uint32_t timeout)
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
    std::cerr << "No GigE Vision devices discovered in this system"
              << "\n";
    return 1;
  }

  // Print number of found devices
  std::cout << "Number of found devices: " << numCameras << "\n\n";
  // end::device_discovery[]

  // tag::identify_target_device[]
  DiscoveredCamera targetCamera;
  bool foundTarget = false;

  for (size_t i = 0; i < numCameras; ++i)
  {
    DiscoveredCamera discoveredCamera;
    gsDiscoveryGetDiscoveredCamera(discovery, i, discoveredCamera.ptr(), error.ptr());
    throwOnError(error);

    auto foundSerialNum = gsDiscoveredCameraGetSerialNumber(discoveredCamera, error.ptr());
    throwOnError(error);

    if (serialNum == foundSerialNum)
    {
      targetCamera = std::move(discoveredCamera);
      foundTarget = true;
      std::cout << "  -> Target device found!\n";
      break;
    }
  }

  if (!foundTarget)
  {
    std::cerr << "Device with serial number '" << serialNum << "' not found\n";
    std::cerr << "The following devices have been found\n";
    for (size_t i = 0; i < numCameras; ++i)
    {
      DiscoveredCamera discoveredCamera;
      gsDiscoveryGetDiscoveredCamera(discovery, i, discoveredCamera.ptr(), error.ptr());
      throwOnError(error);
      printDeviceInfo(discoveredCamera, error);
    }
    return 1;
  }
  // end::identify_target_device[]

  // tag::access_status[]
  auto accessStatus = gsDiscoveredCameraGetAccessStatus(targetCamera, error.ptr());
  throwOnError(error);
  if (accessStatus != egsAccessStatusReadWrite)
  {
    std::cerr << "The discovered camera is not ready to open"
              << "\n";
    return 1;
  }
  // end::access_status[]

  // tag::connect_to_device[]
  Camera camera;
  gsDiscoveryConnectToCamera(discovery, targetCamera, camera.ptr(), error.ptr());
  throwOnError(error);
  // end::connect_to_device[]

  // tag::create_parameters_handle_object[]
  Parameters parameters;
  gsCameraCreateParameters(camera, parameters.ptr(), error.ptr());
  throwOnError(error);
  // end::create_parameters_handle_object[]

  // tag::get_temperature[]
  double systemTemperature = 0.0;
  gsParametersSetEnum(parameters, "DeviceTemperatureSelector", "System", error.ptr());
  throwOnError(error);
  gsParametersGetFloat(parameters, "DeviceTemperature", &systemTemperature, error.ptr());
  throwOnError(error);
  // end::get_temperature[]

  // tag::get_voltage[]
  double deviceVoltage = 0.0;
  gsParametersSetEnum(parameters, "DeviceVoltageSelector", "InputOperating", error.ptr());
  throwOnError(error);
  gsParametersGetFloat(parameters, "DeviceVoltage", &deviceVoltage, error.ptr());
  throwOnError(error);
  // end::get_voltage[]

  // tag::print_parameters[]
  std::cout << "\nDevice Temperature: " << systemTemperature << " degrees Celsius (°C)\n";
  std::cout << "\nDevice voltage: " << deviceVoltage << " Volts (V)\n";
  // end::print_parameters[]

  return 0;
}

int main(int argc, char* argv[])
{
  constexpr unsigned defBroadcastTimeout = 1000u;

  unsigned int timeoutMs = defBroadcastTimeout;
  int exitCode = 0;
  bool showHelpAndExit = false;
  std::string serialNumber;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i)
  {
    std::istringstream argstream(argv[i]);

    if (argstream.get() != '-')
    {
      showHelpAndExit = true;
      exitCode = 1;
      break;
    }
    switch (argstream.get())
    {
      case 'h':
        showHelpAndExit = true;
        break;
      case 's':
        argstream >> serialNumber;
        break;
      case 't':
        argstream >> timeoutMs;
        break;
      default:
        showHelpAndExit = true;
        exitCode = 1;
        break;
    }
  }

  // Validate parameters
  if (serialNumber.empty())
  {
    showHelpAndExit = true;
    exitCode = 1;
  }

  if (showHelpAndExit)
  {
    std::cout << "\n" << argv[0] << " [option]*\n";
    std::cout << "where option is one of:\n";
    std::cout << "  -h              show this help and exit\n";
    std::cout << "  -s<serial>      device serial number (required, e.g., "
                 "-s23070123)\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds "
                 "(default: 5000)\n";
    std::cout << "\n";
    std::cout << "This sample reads the device state (temperature and voltage) "
                 "from a specific\n";
    std::cout << "SICK Visionary device identified by its serial number. The "
                 "device must be\n";
    std::cout << "reachable on the network.\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << argv[0] << " -s23070123\n";
    std::cout << "  " << argv[0] << " -s23070123 -t1000\n";

    return exitCode;
  }

  // Run the configuration
  try
  {
    exitCode = runDeviceState(serialNumber, timeoutMs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = 1;
  }

  std::cout << "\nexit code " << exitCode << "\n";

  return exitCode;
}