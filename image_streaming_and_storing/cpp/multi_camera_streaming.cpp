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
#include <map>
#include <string>

#include "helpers.h"
#include "simplebmp.h"

// PFNC.h is an official header built by the GenICam committee, providing list of the standard
// pixel format identifiers and few related helper functions.
#include "genicam/PFNC.h"

static int runMultiCameraStreaming(const std::vector<std::string>& serialNumbers, uint32_t timeoutMs,
                                   unsigned numFrames, bool writeImgs)
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

  const size_t numTargetDevices = serialNumbers.size();

  std::map<std::string, DiscoveredCamera> targetCameras{};
  size_t matchesFound = 0;

  for (auto i = 0; i < numCameras; ++i)
  {
    DiscoveredCamera discoveredCamera;
    gsDiscoveryGetDiscoveredCamera(discovery, i, discoveredCamera.ptr(), error.ptr());
    throwOnError(error);
    auto foundSerialNum = std::string(gsDiscoveredCameraGetSerialNumber(discoveredCamera, error.ptr()));
    throwOnError(error);

    // auto it = serialNumbers.find(foundSerialNum);
    if (std::find(serialNumbers.begin(), serialNumbers.end(), foundSerialNum) != serialNumbers.end())
    {
      if (!targetCameras[foundSerialNum])
      {
        targetCameras[foundSerialNum] = std::move(discoveredCamera);
        ++matchesFound;

        std::cout << "Found target device w/serial number: " << foundSerialNum << "\n";
        if (matchesFound == numTargetDevices) break;
      }
    }
  }

  // Post-check: report any target serials that were not found
  if (matchesFound != numTargetDevices)
  {
    for (auto& serial : serialNumbers)
    {
      if (!targetCameras[serial])
      {
        std::cerr << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
                  << "\n";
        std::cerr << "Target device NOT FOUND for serial: " << serial << "\n";
        std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
                  << "\n\n";
      }
    }
    std::cout << "The following devices have been found:\n";
    for (size_t i = 0; i < numCameras; ++i)
    {
      DiscoveredCamera discoveredCamera;
      gsDiscoveryGetDiscoveredCamera(discovery, i, discoveredCamera.ptr(), error.ptr());
      throwOnError(error);
      printDeviceInfo(discoveredCamera, error);
    }
    return EXIT_FAILURE;
  }
  // end::device_discovery[]

  // tag::access_status[]
  for (auto& cam : targetCameras)
  {
    auto accessStatus = gsDiscoveredCameraGetAccessStatus(cam.second, error.ptr());
    throwOnError(error);
    if (accessStatus != egsAccessStatusReadWrite)
    {
      std::cerr << "The discovered camera is not ready to open"
                << "\n";
      return EXIT_FAILURE;
    }
  }
  std::cout << "Discovered devices are ready to open\n\n";
  // end::access_status[]

  std::map<std::string, Camera> cameras{};
  std::map<std::string, Parameters> params{};
  for (auto& targetCam : targetCameras)
  {
    // tag::camera_connection[]
    Camera camera;
    gsDiscoveryConnectToCamera(discovery, targetCam.second, camera.ptr(), error.ptr());
    cameras[targetCam.first] = std::move(camera);
    throwOnError(error);
    // end::camera_connection[]

    // tag::camera_parameters[]
    Parameters parameters;
    gsCameraCreateParameters(cameras[targetCam.first], parameters.ptr(), error.ptr());
    params[targetCam.first] = std::move(parameters);
    throwOnError(error);
    // end::camera_parameters[]

    // tag::enable_components[]
    gsParametersSetEnum(params[targetCam.first], "ComponentSelector", "Range", error.ptr());
    throwOnError(error);
    gsParametersSetBool(params[targetCam.first], "ComponentEnable", GsTrue, error.ptr());
    throwOnError(error);
    gsParametersSetEnum(params[targetCam.first], "ComponentSelector", "Intensity", error.ptr());
    throwOnError(error);
    gsParametersSetBool(params[targetCam.first], "ComponentEnable", GsTrue, error.ptr());
    throwOnError(error);
    // end::enable_components[]

    // tag::check_component[]
    if (GsIsTrue(gsParametersIsImplementedEnumEntry(params[targetCam.first], "ComponentSelector", "ImuBasic",
                                                    GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(params[targetCam.first], "ComponentSelector", "ImuBasic", error.ptr());
      throwOnError(error);
      gsParametersSetBool(params[targetCam.first], "ComponentEnable", GsFalse, error.ptr());
      throwOnError(error);
    }
    // end::check_component[]

    // tag:chunk_mode[]
    gsParametersSetBool(params[targetCam.first], "ChunkModeActive", GsTrue, error.ptr());
    throwOnError(error);
    // end:chunk_mode[]

    // tag::adjust_framerate[]
    if (GsIsTrue(gsParametersIsImplemented(params[targetCam.first], "ExposureAuto", GsIgnoreErrorInfo)))
    {
      // ExposureAuto functionality is currently only implemented for the Range exposure on Visionary cameras.
      if (GsIsTrue(gsParametersIsImplemented(params[targetCam.first], "ExposureTimeSelector", GsIgnoreErrorInfo)))
      {
        gsParametersSetEnum(params[targetCam.first], "ExposureTimeSelector", "Range", error.ptr());
        throwOnError(error);
      }

      gsParametersSetEnum(params[targetCam.first], "ExposureAuto", "Off", error.ptr());
      throwOnError(error);
    }
    gsParametersSetFloat(params[targetCam.first], "AcquisitionFrameRate", 5.0, error.ptr()); // [Hz]
    throwOnError(error);
    gsParametersSetInt(params[targetCam.first], "GevSCPD", 100000, error.ptr()); // (packet delay [ns]
    throwOnError(error);
    // end::adjust_framerate[]

    // tag::set_camera_params[]
    if (GsIsTrue(gsParametersIsImplemented(params[targetCam.first], "ExposureTimeSelector", GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(params[targetCam.first], "ExposureTimeSelector", "Range", error.ptr());
      throwOnError(error);
    }
    if (GsIsTrue(gsParametersIsImplemented(params[targetCam.first], "MultiSlopeMode", GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(params[targetCam.first], "MultiSlopeMode", "Off", error.ptr());
      throwOnError(error);
    }
    if (GsIsTrue(gsParametersIsImplemented(params[targetCam.first], "ExposureTime", GsIgnoreErrorInfo)))
    {
      double maxExpTime = 0.0;
      gsParametersGetFloatMax(params[targetCam.first], "ExposureTime", &maxExpTime, error.ptr());
      throwOnError(error);
      gsParametersSetFloat(params[targetCam.first], "ExposureTime", std::min(25000.0, maxExpTime), error.ptr());
      throwOnError(error);
    }
    if (GsIsTrue(gsParametersIsImplementedEnumEntry(params[targetCam.first], "Scan3dDataFilterSelector",
                                                    "ValidationFilter", GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(params[targetCam.first], "Scan3dDataFilterSelector", "ValidationFilter", error.ptr());
      throwOnError(error);
      gsParametersSetBool(params[targetCam.first], "Scan3dDataFilterEnable", GsTrue, error.ptr());
      throwOnError(error);
      gsParametersSetInt(params[targetCam.first], "Scan3dDepthValidationFilterLevel", -3, error.ptr());
      throwOnError(error);
    }
    std::cout << "Set parameters for camera: " << targetCam.first << "\n";
    // end::set_camera_params[]

    // tag::packet_size[]
    int64_t packetSize = 0;
    gsParametersGetInt(params[targetCam.first], "GevSCPSPacketSize", &packetSize, error.ptr());
    throwOnError(error);
    std::cout << "Negotiated packet size (" << packetSize << ") for camera w/serial number: " << targetCam.first
              << "\n\n";

    int64_t maxPacketSize = 0;
    gsParametersGetIntMax(params[targetCam.first], "GevSCPSPacketSize", &maxPacketSize, error.ptr());
    throwOnError(error);
    if (packetSize <= 1500 && maxPacketSize > 1500)
    {
      std::cout << "BEWARE: failed to negotiate larger packet size (>1500B), enable jumbo frame support on the network "
                   "card for optimal performance"
                << "\n";
    }
    // end::packet_size[]
  }

  // tag::frame_grabber[]
  std::map<std::string, FrameGrabber> frameGrabbers{};
  const size_t numBuffers = 10;
  for (auto& cam : cameras)
  {
    FrameGrabber grabber;
    gsCameraCreateFrameGrabber(cam.second, numBuffers, grabber.ptr(), error.ptr());
    throwOnError(error);
    frameGrabbers[cam.first] = std::move(grabber);

    // tag::start_image_acquisition[]
    gsFrameGrabberStart(frameGrabbers[cam.first], error.ptr());
    throwOnError(error);
    // end::start_image_acquisition[]
  }
  // end::frame_grabber[]

  // tag::image_acquisition[]
  std::cout << "Going to acquire " << numFrames << " frame(s)"
            << "\n";
  for (auto iNumAcquired = 0; iNumAcquired < numFrames; /* incremented inside the loop */)
  {
    for (auto& grabber : frameGrabbers)
    {
      GrabResult grabResult;
      gsFrameGrabberGrabNext(grabber.second, grabResult.ptr(), error.ptr());
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
        // depending on the actual camera model you are testing with
        if (intensityFmt != PFNC_BGR8 && intensityFmt != PFNC_Mono16)
        {
          std::cerr << "\tUnexpected pixel format for intensity data, BGR8 or Mono16 data expected"
                    << "\n";
          return 1;
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
          return 1;
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

        // tag::store_components[]
        if (writeImgs)
        {
          auto intensityFile = std::string("intensity") + std::to_string(frameId) + "_" + grabber.first + ".bmp";
          auto intensityPath = std::filesystem::current_path() / "image_streaming_and_storing" / "cpp" / intensityFile;
          intensityPath.make_preferred();
          SimpleBMP::save(static_cast<int>(intensityW), static_cast<int>(intensityH), pIntensityDataBgr,
                          intensityPath.string().c_str());

          auto rangeFile = std::string("range") + std::to_string(frameId) + "_" + grabber.first + ".bmp";
          auto rangePath = std::filesystem::current_path() / "image_streaming_and_storing" / "cpp" / rangeFile;
          rangePath.make_preferred();
          SimpleBMP::save(static_cast<int>(rangeW), static_cast<int>(rangeH), rangeAsBGR.data(),
                          rangePath.string().c_str());
        }
        // end::store_components[]
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
        return 1;
      }
    }
  }

  // tag::stop_acquisition[]
  for (auto& grabber : frameGrabbers)
  {
    gsFrameGrabberStop(grabber.second, error.ptr());
    throwOnError(error);
  }
  // end::stop_acquisition[]

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
  std::vector<std::string> serialNumbers{};
  unsigned numFrames = 1;
  constexpr unsigned defBroadcastTimeout = 1000u;
  unsigned int timeoutMs = defBroadcastTimeout;
  bool writeImgs = false;

  int exitCode = EXIT_SUCCESS;
  bool showHelpAndExit = false;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i)
  {
    std::string token = argv[i];

    if (token.empty() || token[0] != '-')
    {
      // stray value without an option; treat as error
      showHelpAndExit = true;
      exitCode = EXIT_FAILURE;
      break;
    }

    const char opt = token.size() >= 2 ? token[1] : '\0';
    switch (opt)
    {
      case 'h':
        showHelpAndExit = true;
        break;
      case 'w':
        writeImgs = true;
        break;
      case 's': {
        // Support inline value (e.g., -s23070123)
        std::string inlineValue = token.size() > 2 ? token.substr(2) : std::string{};
        if (!inlineValue.empty())
        {
          serialNumbers.push_back(inlineValue);
        }
        // Also consume following argv items as serials until the next option
        while (i + 1 < argc)
        {
          std::string next = argv[i + 1];
          if (!next.empty() && next[0] == '-') break; // next option encountered
          ++i;
          serialNumbers.push_back(next);
        }
        // Validate: at least one serial must have been provided by -s
        if (serialNumbers.empty())
        {
          std::cerr << "Error: at least one serial number must be specified with -s.\n";
          showHelpAndExit = true;
          exitCode = EXIT_FAILURE;
        }
        break;
      }
      case 't': {
        // -t1000 or -t 1000
        std::string inlineValue = token.size() > 2 ? token.substr(2) : std::string{};
        try
        {
          if (!inlineValue.empty())
          {
            timeoutMs = static_cast<unsigned int>(std::stoul(inlineValue));
          }
          else if (i + 1 < argc)
          {
            timeoutMs = static_cast<unsigned int>(std::stoul(argv[++i]));
          }
          else
          {
            showHelpAndExit = true;
            exitCode = EXIT_FAILURE;
          }
        }
        catch (...)
        {
          showHelpAndExit = true;
          exitCode = EXIT_FAILURE;
        }
        break;
      }
      case 'n': {
        // -n10 or -n 10
        std::string inlineValue = token.size() > 2 ? token.substr(2) : std::string{};
        try
        {
          if (!inlineValue.empty())
          {
            numFrames = static_cast<unsigned int>(std::stoul(inlineValue));
          }
          else if (i + 1 < argc)
          {
            numFrames = static_cast<unsigned int>(std::stoul(argv[++i]));
          }
          else
          {
            showHelpAndExit = true;
            exitCode = EXIT_FAILURE;
          }
        }
        catch (...)
        {
          showHelpAndExit = true;
          exitCode = EXIT_FAILURE;
        }
        break;
      }

      default:
        showHelpAndExit = true;
        exitCode = EXIT_FAILURE;
        break;
    }
  }

  if (!showHelpAndExit && serialNumbers.empty())
  {
    std::cerr << "Error: at least one serial number must be specified with -s.\n";
    showHelpAndExit = true;
    exitCode = EXIT_FAILURE;
  }

  if (showHelpAndExit)
  {
    std::cout << "\n" << argv[0] << " [option]*\n";
    std::cout << "where option is one of:\n";
    std::cout << "  -h              show this help and exit\n";
    std::cout << "  -s<serialNumbers>      serial number(s) of target device(s) (required, e.g., -s23070123)\n";
    std::cout << "  -n<numFrames>   number of frames to get (default: 1)\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds (default: 1000)\n";
    std::cout << "  -w<writeImgs>   write images to source folder\n";
    std::cout << "\n";
    std::cout << "This sample shows how to stream and store images from multiple SICK Visionary GigE devices\n";
    std::cout << "identified by their serial number. The devices must be reachable on the network.\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << argv[0] << " -s23070123 or -s 23070123 24000001 ...\n";
    std::cout << "  " << argv[0] << " -s23070123 -t1000\n";
    std::cout << "  " << argv[0] << " -s23070123 -t1000 -n10\n";

    return exitCode;
  }

  // Run the configuration
  try
  {
    exitCode = runMultiCameraStreaming(serialNumbers, timeoutMs, numFrames, writeImgs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = EXIT_FAILURE;
  }

  std::cout << "\nExit code " << exitCode << "\n";

  return exitCode;
}