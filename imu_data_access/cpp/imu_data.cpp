//
// Copyright (c) 2026 SICK AG, Waldkirch
//
// SPDX-License-Identifier: MIT

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "GenIStreamC.h"
using namespace gsc;
#include "GenIStreamCHelpers.hpp"
using namespace gscx;
#include "genicam/PFNC.h"
#include "helpers.h"

static int runImuSample(const std::string& serialNum, uint32_t timeoutMs)
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
  gsDiscoveryScanForCameras(discovery, timeoutMs, error.ptr());
  throwOnError(error, "Failed to scan for cameras: ");

  // Get number of discovered cameras
  auto numCameras = gsDiscoveryGetNumDiscoveredCameras(discovery, error.ptr());
  throwOnError(error, "Failed to get number of cameras: ");

  if (numCameras == 0)
  {
    std::cerr << "No GigE Vision devices discovered\n";
    return EXIT_FAILURE;
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

    auto pFoundSerialNum = gsDiscoveredCameraGetSerialNumber(discoveredCamera, error.ptr());
    throwOnError(error);

    if (serialNum == pFoundSerialNum)
    {
      targetCamera = std::move(discoveredCamera);
      foundTarget = true;
      std::cout << "Found target device\n\n";
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
    return EXIT_FAILURE;
  }
  // end::identify_target_device[]

  // tag::access_status[]
  auto accessStatus = gsDiscoveredCameraGetAccessStatus(targetCamera, error.ptr());
  throwOnError(error);
  if (accessStatus != egsAccessStatusReadWrite)
  {
    std::cerr << "The discovered camera is not ready to open"
              << "\n";
    return EXIT_FAILURE;
  }
  // end::access_status[]

  // tag::camera_connection[]
  Camera camera;
  gsDiscoveryConnectToCamera(discovery, targetCamera, camera.ptr(), error.ptr());
  throwOnError(error);
  // end::camera_connection[]

  // tag::camera_parameters[]
  Parameters parameters;
  gsCameraCreateParameters(camera, parameters.ptr(), error.ptr());
  throwOnError(error);
  // end::camera_parameters[]

  // tag::check_component[]
  if (GsIsTrue(gsParametersIsImplementedEnumEntry(parameters, "ComponentSelector", "ImuBasic", GsIgnoreErrorInfo)))
  // end::check_component[]
  {
    // tag::enable_component[]
    gsParametersSetEnum(parameters, "ComponentSelector", "ImuBasic", error.ptr());
    throwOnError(error);
    gsParametersSetBool(parameters, "ComponentEnable", GsTrue, error.ptr());
    throwOnError(error);
    // end::enable_component[]
  }
  else
  {
    auto pDisplayName = gsDiscoveredCameraGetDisplayName(targetCamera, error.ptr());
    std::cerr << "ImuBasic component is not implemented for " << pDisplayName << ".\n";
    return EXIT_SUCCESS;
  }

  // tag::frame_grabber[]
  const size_t numBuffers = 10;
  FrameGrabber grabber;
  gsCameraCreateFrameGrabber(camera, numBuffers, grabber.ptr(), error.ptr());
  throwOnError(error);
  gsFrameGrabberStart(grabber, error.ptr());
  throwOnError(error);
  std::cout << "Going to acquire a frame including the IMU data component" << '\n';
  // end::frame_grabber[]

  // tag::image_acquisition[]
  GrabResult grabResult;
  gsFrameGrabberGrabNext(grabber, grabResult.ptr(), error.ptr());
  throwOnError(error);
  if (GsIsTrue(gsGrabResultHasLostFrames(grabResult, GsIgnoreErrorInfo)))
  {
    // release and reuse the grab-result handle wrapper
    grabResult.release();
    gsFrameGrabberGrabNext(grabber, grabResult.ptr(), error.ptr());
    throwOnError(error);
  }
  if (GsIsFalse(gsGrabResultHasFrame(grabResult, GsIgnoreErrorInfo)))
  {
    std::cerr << "Failed to acquire the frame to read the IMU data" << '\n';
    return 1;
  }
  Frame frame;
  gsGrabResultGetFrame(grabResult, GsFalse, frame.ptr(), error.ptr());
  throwOnError(error);
  std::cout << "Successfully acquired the frame to demonstrate IMU data processing" << '\n';
  if (GsIsTrue(gsFrameIsIncomplete(frame, GsIgnoreErrorInfo)))
  {
    std::cout << "\tBeware though, the frame was acquired incomplete (one or more packets lost)" << '\n';
  }
  // end::image_acquisition[]

  // tag::get_component[]
  Component cmpImu;
  gsFrameGetComponent(frame, egsComponentIdImuBasic, cmpImu.ptr(), error.ptr());
  throwOnError(error);
  auto imuW = gsComponentGetWidth(cmpImu, GsIgnoreErrorInfo);
  auto imuH = gsComponentGetDeliveredHeight(cmpImu, GsIgnoreErrorInfo);
  auto imuFmt = gsComponentGetPixelFormat(cmpImu, GsIgnoreErrorInfo);
  std::cout << "The frame contains IMU data component, size: " << imuW << "x" << imuH << ", pixel format "
            << GetPixelFormatName(PfncFormat(imuFmt)) << '\n';
  // The IMU-basic data are delivered in Mono8 (8-bit monochrome) format for Visionary
  if (imuFmt != PFNC_Mono8)
  {
    std::cerr << "\tUnexpected pixel format for IMU data, Mono8 data expected" << '\n';
    return 1;
  }
  auto const* imuDataBytes = gsComponentGetData(cmpImu, GsIgnoreErrorInfo);
// end::get_component[]

// tag::parse_imu_data[]
#pragma pack(push, 1)
  struct ImuData
  {
    double acceleration[3];
    double angularVelocity[3];
    double magneticField[3];
    double orientation[4];
    uint64_t timeStamp;
  };
#pragma pack(pop)
  auto const* imuDataValues = reinterpret_cast<const ImuData*>(imuDataBytes);

  // The width of the pseudo-image (single line length) will always match size of this IMU reading struct
  assert(imuW == sizeof(ImuData));
  // Number of IMU readings correspond to the pseudo-image height
  auto numImuReadings = imuH;
  assert(numImuReadings * sizeof(ImuData) == gsComponentGetDeliveredDataSize(cmpImu, GsIgnoreErrorInfo));
  std::cout << "Number of IMU data readings available in the buffer: " << numImuReadings << '\n';

  std::cout << "ID  |Accel-X  |Accel-Y  |Accel-Z  |AngVelo-X|AngVelo-Y|AngVelo-Z|Timestamp" << '\n';
  for (size_t i = 0; i < numImuReadings; ++i)
  {
    auto& imuValue = imuDataValues[i];

    std::cout << std::setfill(' ') //<< std::fixed
              << std::setw(4) << i << "|" << std::setw(9) << std::setprecision(3) << imuValue.acceleration[0] << "|"
              << std::setw(9) << std::setprecision(3) << imuValue.acceleration[1] << "|" << std::setw(9)
              << std::setprecision(3) << imuValue.acceleration[2] << "|" << std::setw(9) << std::setprecision(3)
              << imuValue.angularVelocity[0] << "|" << std::setw(9) << std::setprecision(3)
              << imuValue.angularVelocity[1] << "|" << std::setw(9) << std::setprecision(3)
              << imuValue.angularVelocity[2] << "|" << imuValue.timeStamp << '\n';
  }
  // end::parse_imu_data[]
  return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
  std::string serialNumber;
  constexpr unsigned defBroadcastTimeout = 500u;
  unsigned int timeoutMs = defBroadcastTimeout;

  int exitCode = EXIT_SUCCESS;
  bool showHelpAndExit = false;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i)
  {
    std::istringstream argstream(argv[i]);

    if (argstream.get() != '-')
    {
      showHelpAndExit = true;
      exitCode = EXIT_FAILURE;
      break;
    }
    switch (argstream.get())
    {
      case 'h':
        showHelpAndExit = true;
        break;
      case 's':
        argstream >> serialNumber;
        // Validate parameter
        if (serialNumber.empty())
        {
          showHelpAndExit = true;
          exitCode = EXIT_FAILURE;
        }
        break;
      case 't':
        argstream >> timeoutMs;
        break;
      default:
        showHelpAndExit = true;
        exitCode = EXIT_FAILURE;
        break;
    }
  }

  if (showHelpAndExit)
  {
    std::cout << "\n" << argv[0] << " [option]*\n";
    std::cout << "where option is one of:\n";
    std::cout << "  -h              show this help and exit\n";
    std::cout << "  -s<serial>      serial number of target device (required, e.g., -s23070123)\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds (default: 1100)\n";
    std::cout << "\n";
    std::cout << "This sample shows how to read and interpret IMU data from a SICK Visionary GigE device.\n";
    std::cout << "The device must be reachable on the network and is identified by its serial number.\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << argv[0] << " -s23070123\n";
    std::cout << "  " << argv[0] << " -s23070123 -t1000\n";

    return exitCode;
  }

  // Run the configuration
  try
  {
    exitCode = runImuSample(serialNumber, timeoutMs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = EXIT_FAILURE;
  }
  std::cout << "\nExit code " << exitCode << "\n";
  return exitCode;
}
