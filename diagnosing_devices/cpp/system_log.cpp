//
// Copyright (c) 2025 SICK AG, Waldkirch
//
// SPDX-License-Identifier: MIT

#include "GenIStreamC.h"
using namespace gsc;

#include "GenIStreamCHelpers.hpp"
using namespace gscx;

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "helpers.h"

static int runSystemLog(const std::string& serialNum, uint32_t timeout)
{
  try
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
      std::cerr << "No GigE Vision devices discovered in this system" << std::endl;
      return 1;
    }
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
        std::cout << "  -> Target device found!\n\n";
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
    throwOnError(error, "Failed to get access status of the discovered camera");
    if (accessStatus != egsAccessStatusReadWrite)
    {
      std::cerr << "The discovered camera is not ready to open" << std::endl;
      return 1;
    }
    // end::access_status[]

    // tag::connect_to_device[]
    Camera camera;
    gsDiscoveryConnectToCamera(discovery, targetCamera, camera.ptr(), error.ptr());
    throwOnError(error, "Error connecting to camera");
    // end::connect_to_device[]

    // tag::check_system_log_file[]
    const char* pDeviceFileName = "SystemMessageLog";

    if (GsIsFalse(gsCameraIsImplementedFile(camera, pDeviceFileName, GsIgnoreErrorInfo)))
    {
      /* (You can verify existence of the file before use, if needed) */
      std::cerr << "The file '" << pDeviceFileName << "' is not implemented by the device" << std::endl;
      return 1;
    }

    CameraFile deviceFile;
    gsCameraGetFile(camera, pDeviceFileName, deviceFile.ptr(), error.ptr());
    throwOnError(error, "Failed to get device file");
    if (GsIsFalse(gsCameraFileIsReadable(deviceFile, GsIgnoreErrorInfo)))
    {
      /* (Future firmware versions might introduce write-only files for specific
       * purposes, however, currently supported files are all read-only and thus
       * this check is not required) */
      std::cerr << "The file '" << pDeviceFileName << "' is not readable" << std::endl;
    }
    // end::check_system_log_file[]

    // tag::prepare_filesystem_save_location[]
    auto currentPath = std::filesystem::current_path();
    auto systemLogsPath = currentPath / "diagnosing_devices" / "cpp" / "SystemLogs";

    if (!std::filesystem::exists(systemLogsPath))
    {
      std::cout << "Directory does not exist: " << systemLogsPath << "\n";

      try
      {
        std::filesystem::create_directories(systemLogsPath);
        std::cout << "Created directory: " << systemLogsPath << "\n";
      }
      catch (const std::filesystem::filesystem_error& e)
      {
        std::cerr << "Failed to create directory: " << systemLogsPath << " - " << e.what() << "\n";
        return 1;
      }
    }
    // end::prepare_filesystem_save_location[]

    // tag::collect_file_name_parts[]
    auto model = gsDiscoveredCameraGetModel(targetCamera, error.ptr());
    throwOnError(error);

    auto serialNumber = gsDiscoveredCameraGetSerialNumber(targetCamera, error.ptr());
    throwOnError(error);

    auto now = std::chrono::system_clock::now();
    std::time_t timeTNow = std::chrono::system_clock::to_time_t(now);
    std::tm* pTimeInfo = std::localtime(&timeTNow);

    std::ostringstream timestampStream;
    timestampStream << std::put_time(pTimeInfo, "%Y%m%d_%H%M%S");
    std::string timestamp = timestampStream.str();
    // end::collect_file_name_parts[]

    // tag::retrieve_file_from_camera[]
    std::string logFileName = std::string(model) + "_" + serialNumber + "_" + timestamp + ".txt";
    auto targetFilePath = systemLogsPath / logFileName;
    auto targetFilePathString = targetFilePath.string();
    gsCameraFileRetrieveFromCamera(deviceFile, targetFilePathString.c_str(), error.ptr());
    throwOnError(error);

    std::cout << "Downloaded device file '" << pDeviceFileName << "' to " << targetFilePath << std::endl;
    // end::retrieve_file_from_camera[]
  }
  catch (std::runtime_error& e)
  {
    std::cerr << "A GenIStreamC call failed within the example code: " << std::endl;
    std::cerr << e.what() << std::endl;
    return 1;
  }

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
    std::cout << "\nwhere option is one of:\n";
    std::cout << "  -h              show this help and exit\n";
    std::cout << "  -s<serial>      device serial number (required, e.g., "
                 "-s23070123)\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds "
                 "(default: 5000)\n";
    std::cout << "\n";
    std::cout << "This sample retrieves the system log file from a specific SICK\n";
    std::cout << "Visionary device identified by its serial number. The device "
                 "must be\n";
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
    exitCode = runSystemLog(serialNumber, timeoutMs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = 1;
  }

  std::cout << "\nexit code " << exitCode << "\n";

  return exitCode;
}