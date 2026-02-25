//
// Copyright (c) 2025 SICK AG, Waldkirch
//
// SPDX-License-Identifier: MIT

#include "GenIStreamC.h"
using namespace gsc;

#include "GenIStreamCHelpers.hpp"
using namespace gscx;

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "helpers.h"
#include "simplebmp.h"

// PFNC.h is an official header built by the GenICam committee, providing list of the standard
// pixel format identifiers and few related helper functions.
#include "genicam/PFNC.h"

static int runImageStreaming(const std::string& serialNum, uint32_t timeoutMs, unsigned numFrames, bool writeImgs)
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
  std::cout << "Scanning for devices (timeout: " << timeoutMs << " ms)...\n";
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

  // tag::enable_components[]
  gsParametersSetEnum(parameters, "ComponentSelector", "Range", error.ptr());
  throwOnError(error);
  gsParametersSetBool(parameters, "ComponentEnable", GsTrue, error.ptr());
  throwOnError(error);
  gsParametersSetEnum(parameters, "ComponentSelector", "Intensity", error.ptr());
  throwOnError(error);
  gsParametersSetBool(parameters, "ComponentEnable", GsTrue, error.ptr());
  throwOnError(error);
  // end::enable_components[]

  // tag::check_component[]
  if (GsIsTrue(gsParametersIsImplementedEnumEntry(parameters, "ComponentSelector", "ImuBasic", GsIgnoreErrorInfo)))
  {
    gsParametersSetEnum(parameters, "ComponentSelector", "ImuBasic", error.ptr());
    throwOnError(error);
    gsParametersSetBool(parameters, "ComponentEnable", GsFalse, error.ptr());
    throwOnError(error);
  }
  // end::check_component[]

  // tag::chunk_mode[]
  gsParametersSetBool(parameters, "ChunkModeActive", GsTrue, error.ptr());
  throwOnError(error);
  // end::chunk_mode[]

  // tag::adjust_framerate[]
  if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureAuto", GsIgnoreErrorInfo)))
  {
    // ExposureAuto functionality is currently only implemented for the Range exposure on Visionary cameras.
    if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureTimeSelector", GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(parameters, "ExposureTimeSelector", "Range", error.ptr());
      throwOnError(error);
    }

    gsParametersSetEnum(parameters, "ExposureAuto", "Off", error.ptr());
    throwOnError(error);
  }
  gsParametersSetFloat(parameters, "AcquisitionFrameRate", 5.0, error.ptr()); // [Hz]
  throwOnError(error);
  gsParametersSetInt(parameters, "GevSCPD", 100000, error.ptr()); // (packet delay [ns]
  throwOnError(error);
  // end::adjust_framerate[]

  // tag::set_camera_params[]
  if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureTimeSelector", GsIgnoreErrorInfo)))
  {
    gsParametersSetEnum(parameters, "ExposureTimeSelector", "Range", error.ptr());
    throwOnError(error);
  }
  if (GsIsTrue(gsParametersIsImplemented(parameters, "MultiSlopeMode", GsIgnoreErrorInfo)))
  {
    gsParametersSetEnum(parameters, "MultiSlopeMode", "Off", error.ptr());
    throwOnError(error);
  }
  if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureTime", GsIgnoreErrorInfo)))
  {
    double maxExpTime = 0.0;
    gsParametersGetFloatMax(parameters, "ExposureTime", &maxExpTime, error.ptr());
    throwOnError(error);
    gsParametersSetFloat(parameters, "ExposureTime", std::min(25000.0, maxExpTime), error.ptr());
    throwOnError(error);
  }
  if (GsIsTrue(gsParametersIsImplementedEnumEntry(parameters, "Scan3dDataFilterSelector", "ValidationFilter",
                                                  GsIgnoreErrorInfo)))
  {
    gsParametersSetEnum(parameters, "Scan3dDataFilterSelector", "ValidationFilter", error.ptr());
    throwOnError(error);
    gsParametersSetBool(parameters, "Scan3dDataFilterEnable", GsTrue, error.ptr());
    throwOnError(error);
    gsParametersSetInt(parameters, "Scan3dDepthValidationFilterLevel", -3, error.ptr());
    throwOnError(error);
  }
  // end::set_camera_params[]

  // tag::packet_size[]
  int64_t packetSize = 0;
  gsParametersGetInt(parameters, "GevSCPSPacketSize", &packetSize, error.ptr());
  throwOnError(error);
  std::cout << "Negotiated packet size: " << packetSize << "\n";

  int64_t maxPacketSize = 0;
  gsParametersGetIntMax(parameters, "GevSCPSPacketSize", &maxPacketSize, error.ptr());
  throwOnError(error);
  if (packetSize <= 1500 && maxPacketSize > 1500)
  {
    std::cout << "BEWARE: failed to negotiate larger packet size (>1500B), enable jumbo frame support on the network "
                 "card for optimal performance"
              << "\n";
  }
  // end::packet_size[]

  // tag::frame_grabber[]
  const size_t numBuffers = 10;
  FrameGrabber grabber;
  gsCameraCreateFrameGrabber(camera, numBuffers, grabber.ptr(), error.ptr());
  throwOnError(error);
  // end::frame_grabber[]

  // tag::image_acquisition[]
  gsFrameGrabberStart(grabber, error.ptr());
  throwOnError(error);

  std::cout << "Going to acquire " << numFrames << " frame(s)"
            << "\n";
  for (auto iNumAcquired = 0; iNumAcquired < numFrames; /* incremented inside the loop */)
  {
    GrabResult grabResult;
    gsFrameGrabberGrabNext(grabber, grabResult.ptr(), error.ptr());
    throwOnError(error);

    if (GsIsTrue(gsGrabResultHasFrame(grabResult, GsIgnoreErrorInfo)))
    {
      Frame frame;
      // (GsFalse means we don't want exceptions thrown for incomplete frames)
      gsGrabResultGetFrame(grabResult, GsFalse, frame.ptr(), error.ptr());
      throwOnError(error);
      auto frameId = gsFrameGetId(frame, GsIgnoreErrorInfo);
      std::cout << "\tAcquired frame ID " << frameId << "\n";
      ++iNumAcquired;
      // end::image_acquisition[]

      // tag::get_components[]
      Component cmpRange;
      gsFrameGetRange(frame, cmpRange.ptr(), error.ptr());
      throwOnError(error);
      Component cmpIntensity;
      gsFrameGetIntensity(frame, cmpIntensity.ptr(), error.ptr());
      throwOnError(error);

      auto intensityW = gsComponentGetWidth(cmpIntensity, GsIgnoreErrorInfo);
      auto intensityH = gsComponentGetDeliveredHeight(cmpIntensity, GsIgnoreErrorInfo);
      auto intensityFmt = gsComponentGetPixelFormat(cmpIntensity, GsIgnoreErrorInfo);
      std::cout << "The frame contains intensity component, size: " << intensityW << "x" << intensityH
                << ", pixel format " << GetPixelFormatName(PfncFormat(intensityFmt)) << "\n";
      // The intensity data for Visionary models are expected either in BGR8 format (each pixel consisting
      // of three uint8 values, B-G-R) or in Mono16 format (each pixel being a single uint16 value),
      // depending on the actual camera model you are testing with.
      if (intensityFmt != PFNC_BGR8 && intensityFmt != PFNC_Mono16)
      {
        std::cerr << "\tUnexpected pixel format for intensity data, BGR8 or Mono16 data expected"
                  << "\n";
        return EXIT_FAILURE;
      }
      auto const* pIntensityData = gsComponentGetData(cmpIntensity, GsIgnoreErrorInfo);

      auto rangeW = gsComponentGetWidth(cmpRange, GsIgnoreErrorInfo);
      auto rangeH = gsComponentGetDeliveredHeight(cmpRange, GsIgnoreErrorInfo);
      auto rangeFmt = gsComponentGetPixelFormat(cmpRange, GsIgnoreErrorInfo);
      std::cout << "The frame contains range component, size: " << rangeW << "x" << rangeH << ", pixel format "
                << GetPixelFormatName(PfncFormat(rangeFmt)) << "\n";
      // The range data are expected in COORD_3D_C16 format for Visionary - pixels carrying uint16 values,
      // hence the cast to utilize per-pixel access.
      if (rangeFmt != PFNC_Coord3D_C16)
      {
        std::cerr << "\tUnexpected pixel format for range data, Coord3D_C16 data expected"
                  << "\n";
        return EXIT_FAILURE;
      }
      auto const* pRangeData = reinterpret_cast<const uint16_t*>(gsComponentGetData(cmpRange, GsIgnoreErrorInfo));

      // end::get_components[]

      // tag::format_conversion[]
      std::vector<uint8_t> monoAsBGR; // (converted data if required)
      const uint8_t* pIntensityDataBgr = nullptr;
      if (intensityFmt == PFNC_BGR8)
      {
        // The camera outputs BGR8 which is exactly what we need to for SimpleBMP output.
        pIntensityDataBgr = reinterpret_cast<const uint8_t*>(pIntensityData);
      }
      else
      {
        // The other alternative is Mono16.
        assert(intensityFmt == PFNC_Mono16);
        monoAsBGR = bgr8FromMono16(pIntensityData, intensityW, intensityH);
        pIntensityDataBgr = monoAsBGR.data();
      }
      assert(pIntensityDataBgr != nullptr);
      // The range is delivered as single channel 16-bit data, so we need to build what SimpleBMP needs.
      // Note that this modified image is not intended for further processing, the only goal is to make it easily
      // viewable
      auto rangeAsBGR = bgr8FromMono16(pRangeData, rangeW, rangeH);
      // end::format_conversion[]

      if (writeImgs)
      {
        // tag::store_components[]
        auto intensityFile = std::string("intensity") + std::to_string(frameId) + ".bmp";
        auto intensityPath = std::filesystem::current_path() / "image_streaming_and_storing" / "cpp" / intensityFile;
        intensityPath.make_preferred();
        SimpleBMP::save(static_cast<int>(intensityW), static_cast<int>(intensityH), pIntensityDataBgr,
                        intensityPath.string().c_str());

        auto rangeFile = std::string("range") + std::to_string(frameId) + ".bmp";
        auto rangePath = std::filesystem::current_path() / "image_streaming_and_storing" / "cpp" / rangeFile;
        rangePath.make_preferred();
        SimpleBMP::save(static_cast<int>(rangeW), static_cast<int>(rangeH), rangeAsBGR.data(),
                        rangePath.string().c_str());
        // end::store_components[]
      }
    }
    else if (GsIsTrue(gsGrabResultHasLostFrames(grabResult, GsIgnoreErrorInfo)))
    {
      auto lostFrameCount = gsGrabResultGetLostFrameCount(grabResult, GsIgnoreErrorInfo);
      std::cout << "\tEncountered " << lostFrameCount << " lost frames"
                << "\n";
    }
    else
    {
      std::cerr << "Failed to acquire the frame(s) in a loop"
                << "\n";
      return EXIT_FAILURE;
    }
  }

  gsFrameGrabberStop(grabber, error.ptr());
  throwOnError(error);

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
  std::string serialNumber;
  unsigned numFrames = 1;
  constexpr unsigned defBroadcastTimeout = 500u;
  unsigned int timeoutMs = defBroadcastTimeout;
  bool writeImgs = false;

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
      case 'n':
        argstream >> numFrames;
        break;
      case 'w':
        writeImgs = true;
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
    std::cout << "  -n<numFrames>   number of frames to get (default: 1)\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds (default: 1100)\n";
    std::cout << "  -w<writeImgs>   write images to source folder\n";
    std::cout << "\n";
    std::cout << "This sample shows how to stream and store images from a SICK Visionary GigE device\n";
    std::cout << "identified by its serial number. The device must be reachable on the network.\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << argv[0] << " -s23070123\n";
    std::cout << "  " << argv[0] << " -s23070123 -t1000\n";
    std::cout << "  " << argv[0] << " -s23070123 -t1000 -n10\n";

    return exitCode;
  }

  // Run the configuration
  try
  {
    exitCode = runImageStreaming(serialNumber, timeoutMs, numFrames, writeImgs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = EXIT_FAILURE;
  }

  std::cout << "\nExit code " << exitCode << "\n";

  return exitCode;
}