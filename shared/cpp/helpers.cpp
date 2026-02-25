//
// Copyright (c) 2025 SICK AG, Waldkirch
//
// SPDX-License-Identifier: MIT

#include "helpers.h"

#include "GenIStreamCHelpers.hpp"
using namespace gscx;

#include "GenIStreamC.h"
using namespace gsc;

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

std::string macToString(uint64_t mac)
{
  std::ostringstream ss;
  const size_t bytesInMac = 6;

  for (size_t i = 0; i < bytesInMac; ++i)
  {
    // Shift so the desired byte is in the lowest 8 bits, then mask
    uint64_t byte = (mac >> ((bytesInMac - i - 1) * 8)) & 0xFF;

    ss << std::setfill('0') << std::setw(2) << std::hex << byte;

    if (i != bytesInMac - 1)
    {
      ss << ":";
    }
  }

  return ss.str();
}

std::string getAccessStatusString(int32_t status)
{
  switch (status)
  {
    case 0: // egsAccessStatusUnknown
      return "Unknown";
    case 1: // egsAccessStatusReadWrite
      return "Read/Write";
    case 2: // egsAccessStatusReadOnly
      return "Read Only";
    case 3: // egsAccessStatusNoAccess
      return "No Access";
    case 4: // egsAccessStatusBusy
      return "Busy (Opened by another process)";
    case 5: // egsAccessStatusOpenReadWrite
      return "Open (Read/Write by this process)";
    case 6: // egsAccessStatusOpenRead
      return "Open (Read by this process)";
    default:
      return "Invalid Status";
  }
}

void printDeviceInfo(DiscoveredCamera& discoveredCamera, Error& error)
{
  // tag::print_device_info[]
  // Get device information (strings are valid as long as discoveredCamera handle is valid)
  auto pDisplayName = gsDiscoveredCameraGetDisplayName(discoveredCamera, error.ptr());
  throwOnError(error);

  auto pSerialNumber = gsDiscoveredCameraGetSerialNumber(discoveredCamera, error.ptr());
  throwOnError(error);

  auto macAddress = gsDiscoveredCameraGetMacAddress(discoveredCamera, error.ptr());
  throwOnError(error);
  auto macAddressStr = macToString(macAddress);

  auto ipAddress = gsDiscoveredCameraGetIpAddress(discoveredCamera, error.ptr());
  throwOnError(error);
  String ipAddressStr;
  gsIpAddressToString(ipAddress, ipAddressStr.ptr(), GsIgnoreErrorInfo);
  throwOnError(error);

  auto subnetMask = gsDiscoveredCameraGetSubnetMask(discoveredCamera, error.ptr());
  throwOnError(error);
  String subnetMaskStr;
  gsIpAddressToString(subnetMask, subnetMaskStr.ptr(), GsIgnoreErrorInfo);

  auto accessStatus = gsDiscoveredCameraGetAccessStatus(discoveredCamera, error.ptr());
  throwOnError(error);

  // Print device information
  std::cout << "Device name:  " << pDisplayName << "\n"
            << "SerialNumber: " << pSerialNumber << "\n"
            << "MAC Address:  " << macAddressStr << "\n"
            << "IP Address:   " << ipAddressStr.str() << "\n"
            << "Subnet Mask:  " << subnetMaskStr.str() << "\n"
            << "Access Status: " << getAccessStatusString(accessStatus) << "\n\n";
  // end::print_device_info[]
};

/* Trivial ad-hoc converter transforming 16-bit monochrome image data to a BGR8 format
 * required for the color image output use above in the example. */
std::vector<uint8_t> bgr8FromMono16(void const* pData, size_t width, size_t height)
{
  /* The data is assumed to carry 16-bit monochrome pixels. */
  uint16_t const* pMono16Data = static_cast<uint16_t const*>(pData);

  /* If the 16-bit input is range, depending on the field of view, the actual measured range values can be rather
   * small for the 16-bit range. Similar if the 16-bit input is intensity captured with in low light conditions.
   * Let's roughly scale the data to full range and convert to uint8 dtype before saving (for human eye friendliness).
   * Note that this modified image is not intended for further processing, the only goal is to make it easily
   * viewable.
   * IMPORTANT: this crude brightness adjustment is added here just for convenience when viewing the tutorial
   * outputs, it is NOT assumed to be used in a real application. */
  auto maxDataValue = *std::max_element(pMono16Data, pMono16Data + width * height);
  auto u8Factor = maxDataValue / 255 + 1;
  std::vector<uint8_t> dataAsBGR(width * height * 3);
  for (size_t i = 0; i < width * height; ++i)
  {
    auto grayVal = static_cast<uint8_t>(pMono16Data[i] / u8Factor);
    dataAsBGR[3 * i] = grayVal;
    dataAsBGR[3 * i + 1] = grayVal;
    dataAsBGR[3 * i + 2] = grayVal;
  }

  return dataAsBGR;
}
