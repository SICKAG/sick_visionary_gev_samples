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
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "helpers.h"

// Helper: read Yes/No with validation (returns true for Yes)
bool askYesNo(std::string_view prompt)
{
  std::cout << prompt;
  std::string choice;
  std::getline(std::cin, choice);
  // Trim spaces
  choice.erase(0, choice.find_first_not_of(" \t\r\n"));
  choice.erase(choice.find_last_not_of(" \t\r\n") + 1);
  // Lowercase
  std::transform(choice.begin(), choice.end(), choice.begin(), ::tolower);
  if (choice == "y" || choice == "yes") return true;
  if (choice == "n" || choice == "no")
    return false;
  else
    return false;
};

// Helper to read in IP address or subnet mask
void readAndConvertIp(std::string_view prompt, uint32_t& ipAddress, Error& error)
{
  std::cout << prompt;
  std::string userInputIP;
  std::getline(std::cin, userInputIP);
  // Trim
  userInputIP.erase(0, userInputIP.find_first_not_of(" \t\r\n"));
  userInputIP.erase(userInputIP.find_last_not_of(" \t\r\n") + 1);

  gsIpAddressFromString(userInputIP.c_str(), &ipAddress, error.ptr());
  throwOnError(error, "Failed to convert user input to uint32_t: ");
};

static int runConfigureSensor(const std::string& serialNum, uint32_t timeoutMs, std::optional<std::string> cliIpStr,
                              std::optional<std::string> cliMaskStr, bool writeConfig, bool savePermanent)
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

  // Scan for devices
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

  std::cout << "Found " << numCameras << " device(s)\n\n";

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
      break;
    }
  }

  if (!foundTarget)
  {
    std::cerr << "Device with serial number '" << serialNum << "' not found\n";
    std::cout << "The following devices have been found\n";
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

  // tag::device_info[]
  printDeviceInfo(targetCamera, error);
  auto accessStatus = gsDiscoveredCameraGetAccessStatus(targetCamera, error.ptr());
  throwOnError(error);
  if (accessStatus != egsAccessStatusReadWrite)
  {
    std::cerr << "The discovered camera is not ready to open" << '\n';
    return EXIT_FAILURE;
  }
  // end::device_info[]

  // tag::ip_proposal[]
  // Configure IP address
  uint32_t ipToAssign = 0;
  uint32_t maskToAssign = 0;

  // Auto-propose suitable IP address
  std::cout << "Proposing suitable IP address...\n";
  gsDiscoveryProposeIp(discovery, targetCamera, timeoutMs, &ipToAssign, &maskToAssign, error.ptr());
  throwOnError(error, "Failed to propose IP address: ");

  String proposedIpStr;
  gsIpAddressToString(ipToAssign, proposedIpStr.ptr(), error.ptr());
  throwOnError(error);

  String proposedMaskStr;
  gsIpAddressToString(maskToAssign, proposedMaskStr.ptr(), error.ptr());
  throwOnError(error);
  // end::ip_proposal[]

  std::cout << "  Proposed IP:   " << proposedIpStr.str() << "\n";
  std::cout << "  Proposed Mask: " << proposedMaskStr.str() << "\n";

  const bool ipFromCLI = cliIpStr.has_value();
  const bool maskFromCLI = cliMaskStr.has_value();

  if (ipFromCLI)
  {
    gsIpAddressFromString(cliIpStr->c_str(), &ipToAssign, error.ptr());
    throwOnError(error, "Failed to parse -i<IP> argument: ");
    gsIpAddressToString(ipToAssign, proposedIpStr.ptr(), error.ptr());
    throwOnError(error);
  }

  if (maskFromCLI)
  {
    gsIpAddressFromString(cliMaskStr->c_str(), &maskToAssign, error.ptr());
    throwOnError(error, "Failed to parse -m<mask> argument: ");
    gsIpAddressToString(maskToAssign, proposedMaskStr.ptr(), error.ptr());
    throwOnError(error);
  }

  // tag::user_input_configuration[]
  // If CLI provided IP/mask, skip asking
  bool acceptIp = true;
  bool acceptMask = true;

  if (!ipFromCLI)
  {
    acceptIp = askYesNo("\nUse the proposed IP address? (Yes/No): ");
    if (!acceptIp)
    {
      readAndConvertIp("Please enter a valid dotted IPv4 (e.g., 192.168.1.10):\n", ipToAssign, error);
      gsIpAddressToString(ipToAssign, proposedIpStr.ptr(), error.ptr());
      throwOnError(error);
    }
  }

  if (!maskFromCLI)
  {
    acceptMask = askYesNo("\nUse the proposed Subnet Mask? (Yes/No): ");
    if (!acceptMask)
    {
      readAndConvertIp("Please enter a valid dotted Subnet Mask (e.g., 255.255.255.0):\n", maskToAssign, error);
      gsIpAddressToString(maskToAssign, proposedMaskStr.ptr(), error.ptr());
      throwOnError(error);
    }
  }

  const bool changedByUserOrCLI = (!acceptIp || !acceptMask || ipFromCLI || maskFromCLI);

  if (changedByUserOrCLI)
  {
    std::cout << "\nSelected IP configuration:\n";
    std::cout << "  IP:   " << proposedIpStr.str() << "\n";
    std::cout << "  Mask: " << proposedMaskStr.str() << "\n";

    std::cout << "\nMake sure the IP configuration matches the configuration of the network interface!\n"
              << "(A configuration mismatch will result in camera discovery failure. In this case reset the "
                 "configuration with a reboot.)\n";

    bool acceptProposal = true;
    if (!writeConfig)
    {
      acceptProposal = askYesNo("Force the latest IP configuration on the device? (Yes/No): ");
    }

    if (!acceptProposal)
    {
      std::cerr << "Skipped IP configuration. Exiting Program.\n";
      return EXIT_FAILURE;
    }
  }
  // end::user_input_configuration[]

  // tag::force_ip_config[]
  // Force IP address to device
  std::cout << "\nForcing IP configuration to device...\n";
  gsDiscoveryForceIp(discovery, targetCamera, ipToAssign, maskToAssign, timeoutMs, error.ptr());
  throwOnError(error, "Failed to force IP address: ");
  // end::force_ip_config[]

  std::cout << "IP configuration successfully applied!\n";
  std::cout << "Note: This is a temporary configuration and will be lost after device power cycle.\n";

  // tag::scan_and_verify_new_configuration[]
  // Re-scan to verify new configuration
  std::cout << "\nRe-scanning to verify new configuration...\n";
  gsDiscoveryScanForCameras(discovery, timeoutMs, error.ptr());
  throwOnError(error);

  numCameras = gsDiscoveryGetNumDiscoveredCameras(discovery, error.ptr());
  throwOnError(error);

  // Connect to the camera to be able to configure it
  Camera camera;

  for (size_t i = 0; i < numCameras; ++i)
  {
    DiscoveredCamera discoveredCamera;
    gsDiscoveryGetDiscoveredCamera(discovery, i, discoveredCamera.ptr(), error.ptr());
    throwOnError(error);

    auto pFoundSerialNum = gsDiscoveredCameraGetSerialNumber(discoveredCamera, error.ptr());
    throwOnError(error);

    if (serialNum == pFoundSerialNum)
    {
      printDeviceInfo(discoveredCamera, error);
      gsDiscoveryConnectToCamera(discovery, discoveredCamera, camera.ptr(), error.ptr());
      throwOnError(error);
      break;
    }
  }
  // end::scan_and_verify_new_configuration[]

  // tag::persist_save_configuration[]
  if (savePermanent)
  {
    // access to the device parameters - GenICam map of feature nodes defining user interface of the device
    Parameters parameters;
    gsCameraCreateParameters(camera, parameters.ptr(), error.ptr());
    throwOnError(error);
    gsParametersSetInt(parameters, "GevPersistentIPAddress", ipToAssign, error.ptr());
    throwOnError(error);
    gsParametersSetInt(parameters, "GevPersistentSubnetMask", maskToAssign, error.ptr());
    throwOnError(error);
    gsParametersSetInt(parameters, "GevPersistentDefaultGateway", 0, error.ptr());
    throwOnError(error);
    std::cout << "Permanently saved the current IP configuration.\n";
  }
  // end::persist_save_configuration[]

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
  std::string serialNumber;
  constexpr unsigned defBroadcastTimeout = 1100u;
  unsigned int timeoutMs = defBroadcastTimeout;

  int exitCode = EXIT_FAILURE;
  bool showHelpAndExit = false;

  std::optional<std::string> newIpStr;
  std::optional<std::string> newMaskStr;
  bool writeConfig = false;
  bool savePermanent = false;

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
      case 's':
        argstream >> serialNumber;
        if (serialNumber.empty())
        {
          showHelpAndExit = true;
        }
        break;
      case 't':
        argstream >> timeoutMs;
        break;
      case 'i': {
        std::string ip;
        argstream >> ip;
        if (ip.empty())
        {
          showHelpAndExit = true;
        }
        else
        {
          newIpStr = ip;
        }
        break;
      }
      case 'm': {
        std::string mask;
        argstream >> mask;
        if (mask.empty())
        {
          showHelpAndExit = true;
        }
        else
        {
          newMaskStr = mask;
        }
        break;
      }
      case 'w':
        writeConfig = true;
        break;
      case 'p':
        savePermanent = true;
        break;
      default:
        showHelpAndExit = true;
        break;
    }
  }

  if (showHelpAndExit || serialNumber.empty())
  {
    std::cout << "\n" << argv[0] << " [option]*\n";
    std::cout << "where option is one of:\n";
    std::cout << "  -h              show this help and exit\n";
    std::cout << "  -s<serial>      serial number of target device (required, e.g., -s23070123)\n";
    std::cout << "  -t<timeout>     discovery timeout in milliseconds (default: " << defBroadcastTimeout << ")\n";
    std::cout << "  -i<ip>          IPv4 address to assign (dotted-decimal, e.g., -i192.168.1.10)\n";
    std::cout << "  -m<mask>        Subnet mask to assign (dotted-decimal, e.g., -m255.255.255.0)\n";
    std::cout << "  -w              write(force) configuration without interactive confirmation\n";
    std::cout << "  -p              save configuration permanently without interactive confirmation\n";
    std::cout << "\n";
    std::cout << "This sample configures the IP address of a specific SICK Visionary device\n";
    std::cout << "identified by its serial number. The device must be reachable on the network.\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << argv[0] << " -s23070123\n";
    std::cout << "  " << argv[0] << " -s23070123 -i192.168.1.10 -m255.255.255.0 -w -p\n";
    std::cout << "  " << argv[0] << " -s23070123 -t1500 -i192.168.1.10\n";

    return exitCode;
  }

  // Run the configuration
  try
  {
    exitCode = runConfigureSensor(serialNumber, timeoutMs, newIpStr, newMaskStr, writeConfig, savePermanent);
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    exitCode = EXIT_FAILURE;
  }

  std::cout << "\nExit code " << exitCode << "\n";

  return exitCode;
}
