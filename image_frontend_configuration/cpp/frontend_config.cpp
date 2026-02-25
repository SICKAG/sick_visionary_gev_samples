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

#include "genicam/PFNC.h"
#include "helpers.h"

static int frontendConfig(const std::string& serialNum, uint32_t timeout)
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

    auto pFoundSerialNum = gsDiscoveredCameraGetSerialNumber(discoveredCamera, error.ptr());
    throwOnError(error);

    if (serialNum == pFoundSerialNum)
    {
      targetCamera = std::move(discoveredCamera);
      foundTarget = true;
      std::cout << "  -> Target device found!"
                << "\n\n";
      break;
    }
  }

  if (!foundTarget)
  {
    std::cerr << "Device with serial number '" << serialNum << "' not found"
              << "\n";
    std::cerr << "The following devices have been found"
              << "\n";
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
  throwOnError(error, "Failed to create camera parameters: ");
  // end::create_parameters_handle_object[]

  // tag::set_selector_to_preferred_image_component[]
  // In this case we prefer to work with the intensity image component
  gsParametersSetEnum(parameters, "ComponentSelector", "Intensity", error.ptr());
  throwOnError(error, "Failed to set ComponentSelector to prefered image component: ");
  // end::set_selector_to_preferred_image_component[]

  // tag::enable_previously_selected_image_component[]
  // Enable the acquired image component (Intensity data) to be delivered with the stream
  gsParametersSetBool(parameters, "ComponentEnable", GsTrue, error.ptr());
  throwOnError(error, "Failed to enable previously selected image component: ");
  // end::enable_previously_selected_image_component[]

  // tag::read:out_current_binning_configuration[]
  int64_t currentHorizontalBinning = 0;
  int64_t currentVerticalBinning = 0;
  gsParametersGetInt(parameters, "BinningHorizontal", &currentHorizontalBinning, error.ptr());
  throwOnError(error, "Failed to read out current horizontal binning configuration: ");
  gsParametersGetInt(parameters, "BinningVertical", &currentVerticalBinning, error.ptr());
  throwOnError(error, "Failed to read out current vertical binning configuration: ");

  std::cout << "Current binning configuration:"
            << "\n"
            << "  - Horizontal Binning: " << currentHorizontalBinning << "\n"
            << "  - Vertical Binning: " << currentVerticalBinning << "\n"
            << "\n";
  // end::read:out_current_binning_configuration[]

  // tag::create_frame_grabber_before_configuration[]
  const size_t numBuffers = 10;
  FrameGrabber grabber;
  gsCameraCreateFrameGrabber(camera, numBuffers, grabber.ptr(), error.ptr());
  throwOnError(error);
  // end::create_frame_grabber_before_configuration[]

  // tag::start_frame_grabber_before_configuration[]
  gsFrameGrabberStart(grabber, error.ptr());
  throwOnError(error, "Failed to start frame grabber before configuration: ");
  // end::start_frame_grabber_before_configuration[]

  // tag::acquire_a_new_image_before_configuration[]
  GrabResult grabResult;
  gsFrameGrabberGrabNext(grabber, grabResult.ptr(), error.ptr());
  throwOnError(error, "Failed to acquire a new image before configuration: ");
  // end::acquire_a_new_image_before_configuration[]

  // tag::check_for_corrupted_image_before_configuration[]
  if (GsIsTrue(gsGrabResultHasLostFrames(grabResult, GsIgnoreErrorInfo)))
  {
    grabResult.release();
    gsFrameGrabberGrabNext(grabber, grabResult.ptr(), error.ptr());
    throwOnError(error, "Failed to acquire a new image before configuration after lost frames were detected: ");
  }
  // end::check_for_corrupted_image_before_configuration[]

  // tag::acquire_a_frame_from_image_before_configuration[]
  Frame frame;
  gsGrabResultGetFrame(grabResult, GsFalse, frame.ptr(), error.ptr());
  throwOnError(error);
  std::cout << "Frame aquisition before configuration was successful"
            << "\n\n";

  if (GsIsTrue(gsFrameIsIncomplete(frame, GsIgnoreErrorInfo)))
  {
    std::cout << "\tBeware though, before configuration the frame was acquired incomplete (one or more packets lost)"
              << "\n";
  }
  // end::acquire_a_frame_from_image_before_configuration[]

  // tag::stop_frame_grabber_before_configuration[]
  gsFrameGrabberStop(grabber, error.ptr());
  throwOnError(error);
  // end::stop_frame_grabber_before_configuration[]

  // tag::acquire_image_intensity_component_before_configuration[]
  Component cmpIntensity;
  gsFrameGetIntensity(frame, cmpIntensity.ptr(), error.ptr());
  throwOnError(error);
  // end::acquire_image_intensity_component_before_configuration[]

  // tag::check_binning_configuration_before_configuration[]
  auto intensityW = gsComponentGetWidth(cmpIntensity, GsIgnoreErrorInfo);
  auto intensityH = gsComponentGetDeliveredHeight(cmpIntensity, GsIgnoreErrorInfo);
  auto intensityFmt = gsComponentGetPixelFormat(cmpIntensity, GsIgnoreErrorInfo);

  std::cout << "Before configuration:"
            << "\n"
            << "The acquired intensity component has the following properties:"
            << "\n"
            << "  - Width: " << intensityW << "\n"
            << "  - Height: " << intensityH << "\n"
            << "  - Pixel Format: " << GetPixelFormatName(PfncFormat(intensityFmt)) << "\n"
            << "\n";
  // end::check_binning_configuration_before_configuration[]

  // tag::release_frame_grabber_before_configuration[]
  grabber.release();
  // end::release_frame_grabber_before_configuration[]

  // tag::check_horizontal_binning_option_availability[]
  if (!GsIsTrue(gsParametersIsReadable(parameters, "BinningHorizontal", error.ptr())))
  {
    throw std::runtime_error("BinningHorizontal parameter is not readable");
    return 1;
  }
  if (!GsIsTrue(gsParametersIsWritable(parameters, "BinningHorizontal", error.ptr())))
  {
    throw std::runtime_error("BinningHorizontal parameter is not writable");
    return 1;
  }

  std::cout << "BinningHorizontal parameter is readable and writable"
            << "\n";
  // end::check_horizontal_binning_option_availability[]

  // tag::check_vertical_binning_option_availability[]
  if (!GsIsTrue(gsParametersIsReadable(parameters, "BinningVertical", error.ptr())))
  {
    throw std::runtime_error("BinningVertical parameter is not readable");
    return 1;
  }
  if (!GsIsTrue(gsParametersIsWritable(parameters, "BinningVertical", error.ptr())))
  {
    throw std::runtime_error("BinningVertical parameter is not writable");
    return 1;
  }

  std::cout << "BinningVertical parameter is readable and writable"
            << "\n\n";

  if (!GsIsTrue(gsParametersIsWritable(parameters, "BinningVertical", error.ptr())))
  {
    throw std::runtime_error("BinningVertical parameter is not writable");
    return 1;
  }
  else
  {
  }
  // end::check_vertical_binning_option_availability[]

  // tag::configure_binning_option[]
  int64_t horizontalBinningFactor = 0;
  int64_t verticalBinningFactor = 0;

  auto pDisplayName = gsDiscoveredCameraGetDisplayName(targetCamera, error.ptr());
  throwOnError(error);

  if (std::string(pDisplayName).find("Visionary-T Mini") != std::string::npos)
  {
    std::cout << "Device supports binning factors 1,2 and 4"
              << "\n";
    horizontalBinningFactor = 4; // Set horizontal binning factor to 1,2 or 4 (supported values)
    verticalBinningFactor = 4;   // Set vertical binning factor to 1,2 or 4 (supported values)
  }
  else
  {
    std::cout << "Device does not support binning"
              << "\n\n";
    horizontalBinningFactor = 1;
    verticalBinningFactor = 1;
  }

  gsParametersSetInt(parameters, "BinningHorizontal", horizontalBinningFactor, error.ptr());
  throwOnError(error, "Failed to set BinningHorizontal parameter: ");
  gsParametersSetInt(parameters, "BinningVertical", verticalBinningFactor, error.ptr());
  throwOnError(error, "Failed to set BinningVertical parameter: ");

  std::cout << "New binning configuration:"
            << "\n"
            << "  - Horizontal Binning: " << horizontalBinningFactor << "\n"
            << "  - Vertical Binning: " << verticalBinningFactor << "\n"
            << "\n";
  // end::configure_binning_option[]

  // tag::create_frame_grabber_after_configuration[]
  gsCameraCreateFrameGrabber(camera, numBuffers, grabber.ptr(), error.ptr());
  throwOnError(error, "Failed to recreate frame grabber after configuration: ");
  // end::create_frame_grabber_after_configuration[]

  // tag::start_frame_grabber_after_configuration[]
  gsFrameGrabberStart(grabber, error.ptr());
  throwOnError(error, "Failed to start frame grabber after configuration: ");
  // end::start_frame_grabber_after_configuration[]

  // tag::acquire_a_new_image_after_configuration[]
  grabResult.release();
  gsFrameGrabberGrabNext(grabber, grabResult.ptr(), error.ptr());
  throwOnError(error, "Failed to acquire a new image after configuration: ");
  // end::acquire_a_new_image_after_configuration[]

  // tag::check_for_corrupted_image_after_configuration[]
  if (GsIsTrue(gsGrabResultHasLostFrames(grabResult, GsIgnoreErrorInfo)))
  {
    grabResult.release();
    gsFrameGrabberGrabNext(grabber, grabResult.ptr(), error.ptr());
    throwOnError(error, "Failed to acquire a new image after configuration after lost frames were detected: ");
  }
  // end::check_for_corrupted_image_after_configuration[]

  // tag::acquire_a_frame_from_image_after_configuration[]
  frame.release();
  gsGrabResultGetFrame(grabResult, GsFalse, frame.ptr(), error.ptr());
  throwOnError(error);
  std::cout << "Frame aquisition after configuration was successful"
            << "\n\n";

  if (GsIsTrue(gsFrameIsIncomplete(frame, GsIgnoreErrorInfo)))
  {
    std::cout << "\tBeware though, after configuration the frame was acquired incomplete (one or more packets lost)"
              << "\n";
  }
  // end::acquire_a_frame_from_image_after_configuration[]

  // tag::stop_frame_grabber_after_configuration[]
  gsFrameGrabberStop(grabber, error.ptr());
  throwOnError(error, "Failed to stop frame grabber after configuration: ");
  // end::stop_frame_grabber_after_configuration[]

  // tag::acquire_image_intensity_component_after_configuration[]
  cmpIntensity.release();
  gsFrameGetIntensity(frame, cmpIntensity.ptr(), error.ptr());
  throwOnError(error);
  // end::acquire_image_intensity_component_after_configuration[]

  // tag::check_binning_configuration_after_configuration[]
  intensityW = gsComponentGetWidth(cmpIntensity, GsIgnoreErrorInfo);
  intensityH = gsComponentGetDeliveredHeight(cmpIntensity, GsIgnoreErrorInfo);
  intensityFmt = gsComponentGetPixelFormat(cmpIntensity, GsIgnoreErrorInfo);

  std::cout << "After configuration:"
            << "\n"
            << "The acquired intensity component has the following properties:"
            << "\n"
            << "  - Width: " << intensityW << "\n"
            << "  - Height: " << intensityH << "\n"
            << "  - Pixel Format: " << GetPixelFormatName(PfncFormat(intensityFmt)) << "\n";
  // end::check_binning_configuration_after_configuration[]

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
    std::cout << "  -s<serial>      device serial number (required, e.g., -s23070123)\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds (default: 1000)\n";
    std::cout << "\n";
    std::cout << "This sample demonstrates how to configure image frontend parameters (binning)\n";
    std::cout << "on a SICK Visionary device identified by its serial number. It acquires images\n";
    std::cout << "before and after configuration to verify the effect of the binning settings.\n";
    std::cout << "The device must be reachable on the network.\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << argv[0] << " -s23070123\n";
    std::cout << "  " << argv[0] << " -s23070123 -t500\n";

    return exitCode;
  }

  // Run the configuration
  try
  {
    exitCode = frontendConfig(serialNumber, timeoutMs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = 1;
  }

  std::cout << "\n"
            << "exit code " << exitCode << "\n";

  return exitCode;
}