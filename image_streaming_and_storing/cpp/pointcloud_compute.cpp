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

#include "happly_wrap.h"
#include "helpers.h"
#include "simplebmp.h"

// PFNC.h is an official header built by the GenICam committee, providing list of the standard
// pixel format identifiers and few related helper functions.
#include "genicam/PFNC.h"

static int runPointcloudComputation(const std::string& serialNum, uint32_t timeoutMs, unsigned numFrames, bool writePLY)
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

  // tag:chunk_mode[]
  gsParametersSetBool(parameters, "ChunkModeActive", GsTrue, error.ptr());
  throwOnError(error);
  // end:chunk_mode[]

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
      // depending on the actual camera model you are testing with
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

      // tag::camera_intrinsics[]
      double focalLength = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dFocalLength", &focalLength, error.ptr());
      throwOnError(error);
      double aspectRatio = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dAspectRatio", &aspectRatio, error.ptr());
      throwOnError(error);
      double princPtU = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dPrincipalPointU", &princPtU, error.ptr());
      throwOnError(error);
      double princPtV = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dPrincipalPointV", &princPtV, error.ptr());
      throwOnError(error);

      // Read also the scale / offset to be applied on the "coordinate C" (acquired range value).
      gsParametersSetEnum(parameters, "ChunkScan3dCoordinateSelector", "CoordinateC", error.ptr());
      throwOnError(error);
      double coordCScale = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateScale", &coordCScale, error.ptr());
      throwOnError(error);
      double coordCOffset = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateOffset", &coordCOffset, error.ptr());
      throwOnError(error);

      // Read information about the range value used to flag an invalid pixel (carrying no useful measurement)
      GsBool invalidFlagUsed = GsFalse;
      gsParametersGetBool(parameters, "ChunkScan3dInvalidDataFlag", &invalidFlagUsed, error.ptr());
      throwOnError(error);
      double invalidFlagValue = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dInvalidDataValue", &invalidFlagValue, error.ptr());
      throwOnError(error);

      String outputMode;
      gsParametersGetEnum(parameters, "ChunkScan3dOutputMode", outputMode.ptr(), error.ptr());
      throwOnError(error);

      // Optional: anchor-to-reference coordinate system transformation
      gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "RotationX", error.ptr());
      throwOnError(error);
      double refCoordRotX = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordRotX, error.ptr());
      throwOnError(error);
      gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "RotationY", error.ptr());
      throwOnError(error);
      double refCoordRotY = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordRotY, error.ptr());
      throwOnError(error);
      gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "RotationZ", error.ptr());
      throwOnError(error);
      double refCoordRotZ = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordRotZ, error.ptr());
      throwOnError(error);
      gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "TranslationX", error.ptr());
      throwOnError(error);
      double refCoordTransX = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordTransX, error.ptr());
      throwOnError(error);
      gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "TranslationY", error.ptr());
      throwOnError(error);
      double refCoordTransY = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordTransY, error.ptr());
      throwOnError(error);
      gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "TranslationZ", error.ptr());
      throwOnError(error);
      double refCoordTransZ = 0.0;
      gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordTransZ, error.ptr());
      throwOnError(error);
      // end::camera_intrinsics[]

      std::cout << "Received stream meta data ('chunk data') values:"
                << "\n";
      std::cout << "\tOutput mode: " << outputMode.str() << "\n";
      std::cout << "\tFocal length (in pixels): " << focalLength << "\n";
      std::cout << "\tAspect ratio: " << aspectRatio << "\n";
      std::cout << "\tPrincipal point [U,V]: [" << princPtU << ", " << princPtV << "]"
                << "\n";
      std::cout << "\tCoordinate C (range) scale: " << coordCScale << "\n";
      std::cout << "\tCoordinate C (range) offset: " << coordCOffset << "\n";
      std::cout << "\tInvalid data flag used: " << GsIsTrue(invalidFlagUsed) << "\n";
      std::cout << "\tInvalid data flag value: " << invalidFlagValue << "\n";
      std::cout << "\tOptional reference coordinate transformation parameters:"
                << "\n";
      std::cout << "\t\tRotation X (in degrees): " << refCoordRotX << "\n";
      std::cout << "\t\tRotation Y (in degrees): " << refCoordRotY << "\n";
      std::cout << "\t\tRotation Z (in degrees): " << refCoordRotZ << "\n";
      std::cout << "\t\tTranslation X (in mm): " << refCoordTransX << "\n";
      std::cout << "\t\tTranslation Y (in mm): " << refCoordTransY << "\n";
      std::cout << "\t\tTranslation Z (in mm): " << refCoordTransZ << "\n";

      // tag::compute_pointcloud[]
      std::vector<std::array<double, 3>> pointCoordinates;
      std::vector<std::array<unsigned char, 3>> pointColors;

      auto const maxPoints = rangeW * rangeH;
      pointCoordinates.reserve(maxPoints);
      pointColors.reserve(maxPoints);
      auto const intensityScaleX = intensityW / rangeW;
      auto const intensityScaleY = intensityH / rangeH;
      // Calculating the point coordinates pixel by pixel. Focusing on clear demonstration of the algorithm, not on
      // performance and user app might organize/use the points differently.
      for (auto row = 0u; row < rangeH; ++row)
      {
        for (auto col = 0u; col < rangeW; ++col)
        {
          auto const rangePixelOffset = row * rangeW + col;
          // For simplicity we'll pick directly a single intensity pixel for overlay even if the intensity is scaled,
          // more advanced approach could involve some kind of interpolation.
          auto const intensityPixelOffset = row * intensityScaleY * intensityW + col * intensityScaleX;

          // Raw range-coordinate value delivered by the camera for given pixel
          auto coordCValue = pRangeData[rangePixelOffset];

          // Remember that not all pixels might carry a valid measurement - the invalid ones might be marked using the
          // corresponding invalid data flag. The actual use and value of the flag was read together with the intrinsics
          // from chunk data above. Current version of the Visionary cameras always switch use of the flag ON (true) and
          // the flag value is fixed at 0. Zero delivered range pixels therefore denote invalid pixels - let's filter
          // them out (skip them) of the point cloud output. The following lines anyway rely on the generically
          // retrieved invalid value parameters.
          if (invalidFlagUsed && invalidFlagValue == coordCValue)
          {
            continue;
          }

          // The X / Y coordinate per - pixel multiplicators
          // (Note: these can be also precomputed just once within the acquisition loop, relying on the fact that the
          // intrinsic parameters will not change during the acquisition)
          auto xp = (col - princPtU) / focalLength;
          auto yp = (row - princPtV) / (focalLength * aspectRatio);

          // The actual coordinate values xc / yc / zc for each pixel are all computed from the measured range (depth)
          // values. The distance units used by Visionary are millimeters (could also be "formally" queried from
          // ChunkScan3dDistanceUnit).
          auto scaledC = coordCValue * coordCScale + coordCOffset;
          auto xc = xp * scaledC;
          auto yc = yp * scaledC;
          auto zc = scaledC;
          pointCoordinates.push_back({xc, yc, zc});

          // Note that the computed coordinates are in camera's native "Anchor" coordinate system which is related to
          // its internal geometry and might be affected by current operating mode and sensor mounting deviations.
          // Compare this to the "Reference" coordinate system with origin in the center of camera's front surface and Z
          // pointing out of the camera. When conversion to the Reference coordinate system is required, it can be
          // performed here using the transformation parameters queried above, in following order: 1. refCoordRotX, 2.
          // refCoordRotY, 3. refCoordRotZ, 4. all translations.

          // Store also the R-G-B channel values for this pixel to allow adding the intensity-color overlay to the
          // ouptut PLY file
          auto b = pIntensityDataBgr[3 * intensityPixelOffset];
          auto g = pIntensityDataBgr[3 * intensityPixelOffset + 1];
          auto r = pIntensityDataBgr[3 * intensityPixelOffset + 2];
          pointColors.push_back({r, g, b});
        }
      }
      std::cout << "Number of valid points: " << pointCoordinates.size() << "\n";
      happly::PLYData ply;
      ply.addVertexPositions(pointCoordinates);
      ply.addVertexColors(pointColors);
      if (writePLY)
      {
        auto pointCloudFile = std::string("pointcloud_") + std::to_string(frameId) + ".ply";
        auto pointCloudFp = std::filesystem::current_path() / "image_streaming_and_storing" / "cpp" / pointCloudFile;
        pointCloudFp.make_preferred();

        ply.write(pointCloudFp.string(), happly::DataFormat::Binary);
      }
      // end::compute_pointcloud[]
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
  bool writePLY = false;

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
        writePLY = true;
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
    std::cout << "  -w<writePLY>    write ply to source folder\n";
    std::cout << "\n";
    std::cout << "This sample shows how to use the images from a SICK Visionary GigE device to create a pointcloud.\n";
    std::cout << "The device is identified by its serial number and must be reachable on the network.\n";
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
    exitCode = runPointcloudComputation(serialNumber, timeoutMs, numFrames, writePLY);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = EXIT_FAILURE;
  }

  std::cout << "\nExit code " << exitCode << "\n";

  return exitCode;
}