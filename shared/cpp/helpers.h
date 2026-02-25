//
// Copyright (c) 2025 SICK AG, Waldkirch
//
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>

#include "GenIStreamCHelpers.hpp"
using namespace gscx;

#include "GenIStreamC.h"
using namespace gsc;

/*!
  \brief Converts a 48-bit MAC address (stored in a \c uint64_t) to a human-readable string.

  \details Formats the MAC address as six two-digit hexadecimal bytes separated by colons,
  preserving leading zeros for each byte, e.g. "01:23:45:67:89:ab".
  The function extracts each byte by shifting the input value and masking with 0xFF, producing
  bytes in network order (OUI first, NIC-specific part last).

  \param[in] mac The MAC address value. Only the lowest 48 bits are considered; higher bits are ignored.
  \return A lowercase hexadecimal, colon-separated representation of the MAC address.
*/
std::string macToString(uint64_t mac);

/*!
  \brief Converts a GenIStreamC access status code to a human-readable description.

  \details Maps known numeric status values returned by GenIStreamC discovery APIs to readable strings:
  - 0: "Unknown"
  - 1: "Read/Write"
  - 2: "Read Only"
  - 3: "No Access"
  - 4: "Busy (Opened by another process)"
  - 5: "Open (Read/Write by this process)"
  - 6: "Open (Read by this process)"
  Any value outside the known set yields "Invalid Status".

  \param[in] status The integer access status (e.g., from \c gsDiscoveredCameraGetAccessStatus).
  \return A descriptive string for the given access status.
*/
std::string getAccessStatusString(int32_t status);

/*!
  \brief Prints human-readable device information for a discovered camera.

  \details Queries the provided \ref DiscoveredCamera handle for key properties via the GenIStreamC API:
  display name, serial number, MAC address, IP address, subnet mask, and access status.
  After each API call, the function invokes \c throwOnError(hErr). On success, it prints a formatted block
  to \c std::cout.

  \param[in]  discoveredCamera A valid \ref DiscoveredCamera handle. Returned string views remain valid
                               as long as the handle is valid.
  \param[in,out] hErr          A GenIStreamC error handle updated by the library calls; checked via \c throwOnError.

  \return None (side-effect: writes a formatted info block to \c std::cout).

  \throws Whatever \c throwOnError emits (e.g., \c std::runtime_error) when a GenIStreamC function reports an error.
*/
void printDeviceInfo(DiscoveredCamera& discoveredCamera, Error& error);

std::vector<uint8_t> bgr8FromMono16(void const*, size_t, size_t);