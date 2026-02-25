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

static int runDeviceStateExtended(const std::string& serialNum, uint32_t timeout)
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

  // tag::get_DeviceVendorName[]
  String deviceVendorName;
  gsParametersGetString(parameters, "DeviceVendorName", deviceVendorName.ptr(), error.ptr());
  throwOnError(error);
  // end::get_DeviceVendorName[]

  // tag::get_DeviceModelName[]
  String deviceModelName;
  gsParametersGetString(parameters, "DeviceModelName", deviceModelName.ptr(), error.ptr());
  throwOnError(error);
  // end::get_DeviceModelName[]

  // tag::get_DeviceSerialNumber[]
  String deviceSerialNumber;
  gsParametersGetString(parameters, "DeviceSerialNumber", deviceSerialNumber.ptr(), error.ptr());
  throwOnError(error);
  // end::get_DeviceSerialNumber[]

  // tag::get_DeviceVersion[]
  String deviceVersion;
  gsParametersGetString(parameters, "DeviceVersion", deviceVersion.ptr(), error.ptr());
  throwOnError(error);
  // end::get_DeviceVersion[]

  // tag::get_DeviceUserID[]
  String deviceUserID;
  gsParametersGetString(parameters, "DeviceUserID", deviceUserID.ptr(), error.ptr());
  throwOnError(error);
  // end::get_DeviceUserID[]

  // tag::get_DeviceScanType[]
  String deviceScanType;
  gsParametersGetEnum(parameters, "DeviceScanType", deviceScanType.ptr(), error.ptr());
  throwOnError(error);
  // end::get_DeviceScanType[]

  // tag::get_DeviceOperationStatus[]
  String deviceOperationStatus;
  gsParametersGetEnum(parameters, "DeviceOperationStatus", deviceOperationStatus.ptr(), error.ptr());
  throwOnError(error);
  // end::get_DeviceOperationStatus[]

  // tag::get_DeviceOperationDomainStatus_entries[]
  StringList deviceOperationDomainStatusEntries;
  gsParametersGetEnumEntries(parameters, "DeviceScanType", GsTrue, deviceOperationDomainStatusEntries.ptr(),
                             error.ptr());
  throwOnError(error);

  std::cout << "Supported DeviceScanType entries:\n";
  for (const auto& entry : deviceOperationDomainStatusEntries.strs())
  {
    std::cout << " - " << entry << "\n";
  }
  // end::get_DeviceOperationDomainStatus_entries[]

  // tag::get_TemperatureDomainStatus[]
  String deviceOperationTemperatureDomainStatus;
  gsParametersSetEnum(parameters, "DeviceOperationDomainSelector", "Temperature", error.ptr());
  throwOnError(error);
  gsParametersGetEnum(parameters, "DeviceOperationDomainStatus", deviceOperationTemperatureDomainStatus.ptr(),
                      error.ptr());
  throwOnError(error);
  // end::get_TemperatureDomainStatus[]

  // tag::get_ElectricalConnectivityDomainStatus[]
  String deviceOperationElectricalConnectivityDomainStatus;
  gsParametersSetEnum(parameters, "DeviceOperationDomainSelector", "ElectricalConnectivity", error.ptr());
  throwOnError(error);
  gsParametersGetEnum(parameters, "DeviceOperationDomainStatus",
                      deviceOperationElectricalConnectivityDomainStatus.ptr(), error.ptr());
  throwOnError(error);
  // end::get_ElectricalConnectivityDomainStatus[]

  // tag::get_IlluminationDomainStatus[]
  //  The Visionary-B Two doesn't support the illumnination domain because it
  //  has no illumination unit
  std::string deviceModelNameCStdString = deviceModelName.str();
  deviceModelNameCStdString.erase(deviceModelNameCStdString.find(' '));

  String deviceOperationIlluminationDomainStatus;
  if (deviceModelNameCStdString != "Visionary-B")
  {
    gsParametersSetEnum(parameters, "DeviceOperationDomainSelector", "Illumination", error.ptr());
    throwOnError(error);
    gsParametersGetEnum(parameters, "DeviceOperationDomainStatus", deviceOperationIlluminationDomainStatus.ptr(),
                        error.ptr());
    throwOnError(error);
  }
  else
  {
    std::cout << "\nSkipping Illumination domain status retrieval for model: " << deviceModelNameCStdString << "\n";
  }
  // end::get_IlluminationDomainStatus[]

  // tag::get_ImageAcquisitionDomainStatus[]
  String deviceOperationImageAcquisitionDomainStatus;
  gsParametersSetEnum(parameters, "DeviceOperationDomainSelector", "ImageAcquisition", error.ptr());
  throwOnError(error);
  gsParametersGetEnum(parameters, "DeviceOperationDomainStatus", deviceOperationImageAcquisitionDomainStatus.ptr(),
                      error.ptr());
  throwOnError(error);
  // end::get_ImageAcquisitionDomainStatus[]

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
  std::cout << "\nDevice Vendor Name:               " << deviceVendorName.str() << "\n";
  std::cout << "\nDevice Model Name:                " << deviceModelName.str() << "\n";
  std::cout << "\nDevice Serial Number:             " << deviceSerialNumber.str() << "\n";
  ;
  std::cout << "\nDevice Version:                   " << deviceVersion.str() << "\n";
  std::cout << "\nDevice User ID:                   " << deviceUserID.str() << "\n";
  std::cout << "\nDevice Temperature:               " << systemTemperature << " degrees Celsius (°C)\n";
  std::cout << "\nDevice voltage:                   " << deviceVoltage << " Volts (V)\n";
  std::cout << "\nDevice Operation Status:          " << deviceOperationStatus.str() << "\n";

  std::cout << "\nDevice Operation Domain Statuses: \n";
  std::cout << "  - Temperature:                  " << deviceOperationTemperatureDomainStatus.str() << "\n";
  std::cout << "  - Electrical Connectivity:      " << deviceOperationElectricalConnectivityDomainStatus.str() << "\n";
  std::cout << "  - Illumination:                  " << deviceOperationIlluminationDomainStatus.str() << "\n";
  std::cout << "  - Image Acquisition:            " << deviceOperationImageAcquisitionDomainStatus.str() << "\n";
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
    exitCode = runDeviceStateExtended(serialNumber, timeoutMs);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = 1;
  }

  std::cout << "\nexit code " << exitCode << "\n";

  return exitCode;
}