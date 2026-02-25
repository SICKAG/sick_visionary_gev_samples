/**********************************************************************************************
 * TutorialGenIStreamC.cpp: tutorial example demonstrating how to access SICK Visionary cameras
 * with GigE Vision interface using the GenIStreamC API including a small set of C++ helper tools
 * to facilitate accessing the C-API from C++.
 *
 * IMPORTANT: the GenIStreamC API is intended as a primary programming interface to access
 * the cameras from SICK Visionary family, although it is not strictly locked only to
 * these models and should work well with any other SICK product.
 * It has evolved from an older "GenIStream" library designed strictly for SICK Ranger/Ruler
 * cameras. GenIStreamC is its generalized version, providing also a stable ABI. The main
 * components and philosophy, however, remains the same for users familiar with the previous
 * library.
 *
 * This tutorial introduces all important aspects of work with the Visionary cameras.
 * It should be possible to build the entire application around examples shown in the tutorial.
 *
 * Note that although the API itself is in C, the tutorial is implemented in C++, demonstrating
 * also the convenience C++ helpers on top of the API.
 *
 * Copyright (c) 2025 SICK AG, Waldkirch
 * SPDX-License-Identifier: MIT
 *
 **********************************************************************************************/

/* Quick Overview:
 * - Visionary cameras provide standard GigE Vision interface - users relying on GigE Vision compliant
 *   receiver software can use them out of the box.
 * - For users without direct access to a GigE Vision receiver, SICK provides a GigE Vision receiver
 *   implementation which exposes its functionality over a standard GenICam GenTL API.
 * - The receiver is provided as a standard GenTL Producer file (*.cti), one binary for every supported
 *   platform.
 * - The GenTL Producer can be accessed either directly using GenICam GenTL API (https://genicam.org/)
 *   or indirectly through another tool supporting that API.
 * - The GenIStreamC API is one such option, wrapping the GenTL API into a simple to use C interface
 *   with a stable ABI. The GenIStreamC library uses the GenTL Producer implementation under the hood.
 * - The GenIStreamC API comes with detailed reference documentation - we recommend studying it
 *   together with this tutorial whenever required.
 * - Check also brief overview of the system in InterfacingGev.pdf. */

/* GenIStreamC.h header provides a single entry point to the interface, including all other required
 * headers.
 * When compiled in C++, the API lives in the "gsc" namespace - we introduce it here for convenience. */
#include "GenIStreamC.h"
using namespace gsc;

/* On top of the C API itself, the library comes with a small (header only) set of helpers
 * streamlining use of the library from C++. We will use them throughout the example. */
#include "GenIStreamCHelpers.hpp"
using namespace gscx;

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

/* Third party libraries used to store the acquired data in PLY and BMP formats. */
#include "3pp/happly_wrap.h"
#include "3pp/simplebmp.h"

/* PFNC.h is an official header built by the GenICam committee, providing list of the standard
 * pixel format identifiers and few related helper functions. */
#include "genicam/PFNC.h"

/* Forward declarations for helper functions within this example... */
static std::vector<uint8_t> bgr8FromMono16(void const*, size_t, size_t);

/* Main example code */
int main()
{
  /* The convenience C++ wrappers/helpers used within this tutorial turn the C API errors to exceptions
   * for less verbose error handling. To keep the code simple/readable, we install just a single
   * "global" exception handler enclosing the entire tutorial functionality.
   * In a real application more fine granular approach might be required. */
  try
  {
    /* Note that before jumping into practical code showing how to work with the cameras,
     * this example starts a bit slower to introduce some general principles of work
     * with the library, such as error handling and resource management (API object handles). */

    /* Let's start with an overview of error handling in the GenIStreamC API (if required,
     * refer to its reference documentation for further details:
     * - Most functions return a GsRet value, indicating whether the call was successful.
     *   The GsOK macro converts that value to a simple Boolean success/failure result.
     * - There is one exception to the rule above, functions which are simple getters which just
     *   return a simple value and are not expected to fail unless they are called with obviously
     *   broken prerequisites (such as using invalid handle). Such functions return the value
     *   directly instead of GsRet.
     * - All API functions provide an optional last parameter, GsErrorH*, allowing to obtain
     *   a handle to the extended error information (in particular the error message) if the function
     *   fails. This applies to all functions, including pure getters. The error handle will be
     *   created if and only if the function fails. */

    /* Handle for extended error info that will be reused across all functions where we
     * check for error (and wish to receive extended error info). */
    GsErrorH hErr = GsInvalidH;

    /* Before using any other functions, the library must be initialized using gsLibraryInit.
     * Let's first call it with fully detailed error handling to demonstrate how it can get
     * verbose in the pure C API. In a following step we will introduce few C++ helper constructs
     * which will make use of the API more straightforward. */
    if (!GsOK(gsLibraryInit(&hErr)))
    {
      /* The call failed and thus the extended error info handle was also generated.
       * Let's query the error message from it. */
      const char* errMsg = gsErrorCstr(hErr, nullptr);
      if (errMsg == nullptr) /* (extra safety, unexpected) */
      {
        errMsg = "Failed to obtain error message";
      }
      /* The C-string pointer is valid as long as the error handle itself is:
       * use the string before releasing the handle. */
      std::cerr << "Failed to initialize GenIStreamC library: " << errMsg << std::endl;
      /* Same as any other handle/resource created by the library, the error handle has to be
       * released. */
      gsErrorRelease(hErr, nullptr);
      hErr = GsInvalidH;
      return 1;
    }

    /* When finished working with the library, it should be explicitly cleaned up using
     * the gsLibraryClose call.
     * Closing it here immediately just to be able to demonstrate streamlined error handling
     * using the provided C++ helpers.
     * Note that for the cleanup functions we usually cannot do much if they fail (anyway unexpected),
     * so it is also OK to ignore error handling in such cases - or just log the problem if desired. */
    gsLibraryClose(GsIgnoreErrorInfo);

    /* Initialize the library again to demonstrate the streamlined error handling. We will use
     * this approach for the rest of the tutorial.
     * The idea is: use always the last parameter of each function to obtain the extended error info.
     * This allows uniform error checking, no matter if the function returns GsRet or if it is
     * a pure getter function. The throwOnError() helper function will check if there was an error
     * and if yes, it reads the associated error message and turns the error to a C++ exception.
     * It also automatically releases the error handle, if one was created.
     * The second optional parameter of throwOnError() allows to specify a string that will be prepended
     * to the actual error message for extra context, if useful. */
    gsLibraryInit(&hErr);
    throwOnError(hErr, "Failed to initialize GenIStreamC library: ");

    /* In presence of exceptions, it is also useful to perform any cleanup, such as gsLibraryClose,
     * through a RAII mechanism, so that it is always executed, no matter how the current scope
     * is exited (including exception or other early return).
     * The ScopeExit utility provides exactly that. After instantiating the scope exit handler below,
     * we do not need to care about closing the library any more, it will be done always when
     * leaving the current scope (in this case when leaving the try-block surrounding this code). */
    ScopeExit libCloser([=]() { gsLibraryClose(GsIgnoreErrorInfo); });

    /* Instantiate the camera discovery object that will give us access to the camera(s).
     * Any objects/resources provided to the application by the library are represented by their respective
     * handles (such as GsDiscoveryH handle representing the camera discovery object).
     * It is application's responsibility to release any handle as soon as given object/resource
     * is no more needed and definitely before closing the library.
     * Once released, the handle must not be used any more (using an invalid handle can lead to a crash). */
    GsDiscoveryH hDiscovery = GsInvalidH;
    gsLibraryGetDiscovery(&hDiscovery, &hErr);
    throwOnError(hErr);

    /* Second and last hiccup in the example code: release the handle just created to introduce a "better" way
     * of handle lifetime tracking, again with help of the C++ helper tools. */
    gsDiscoveryRelease(hDiscovery, GsIgnoreErrorInfo);
    hDiscovery = GsInvalidH;

    /* Instead of working with the raw handles, the application can use the C++ handle wrappers
     * which provide following functionality:
     * - Automatic lifetime management, similar to ScopeExit. The C-API handle gets released
     *   as soon as the wrapper gets destroyed (goes out of scope).
     * - The wrapper casts automatically to its underlying handle - the wrapper can be passed directly
     *   to the C-API call expecting the handle itself.
     * - Fill the wrapper with help of its ptr() method, passing it to the corresponding handle-creating
     *   API function.
     * - Predictable naming of the wrappers: for example GsDiscoveryH handle type is wrapped in
     *   gscx::Discovery wrapper. Note that the namespaces are introduced (using directive) for the entire
     *   example code above for readability purposes.
     * - If desired, the handle can be released prematurely using the wrapper's release() method. The wrapper
     *   can be reused after that point.*/

    /* Repeat the gsLibraryGetDiscovery() call again, this time using the recommended approach with handle wrappers.
     * The same approach will be used for any other GenIStreamC API handles throughout rest of the example. */
    Discovery discovery;
    gsLibraryGetDiscovery(discovery.ptr(), &hErr);
    throwOnError(hErr);

    /* After introducing the basics, let's focus on actual work with the device, first discovering it in the system.
     * The default device discovery timeout (time to wait for cameras announcing their presence in the network)
     * is sufficient for off-factory Visionary cameras. However, GigE Vision specification defines a configurable
     * delay (typically randomized up to 1 second) the cameras should insert before announcing themselves to avoid
     * flooding the network if many cameras are connected. We'll use longer timeout here to safely discover even cameras
     * configured with longer delay. */
    uint32_t discoveryTimeoutMs = 1100;
    /* At this point the camera must be started and connected to the system running this example... */
    gsDiscoveryScanForCameras(discovery, discoveryTimeoutMs, &hErr);
    throwOnError(hErr);
    /* Exit the example if no devices were found, otherwise print their overview. */
    auto numCameras = gsDiscoveryGetNumDiscoveredCameras(discovery, &hErr);
    throwOnError(hErr);
    if (numCameras == 0)
    {
      std::cerr << "No GigE Vision devices discovered in this system" << std::endl;
      return 1;
    }
    std::cout << "Discovered " << numCameras << " device(s), going to use the first entry in the test:" << std::endl;
    for (int i = 0; i < numCameras; ++i)
    {
      DiscoveredCamera discoveredCamera;
      gsDiscoveryGetDiscoveredCamera(discovery, i, discoveredCamera.ptr(), &hErr);
      throwOnError(hErr);

      /* Note about getter functions returning a string (const char*):
       * Keep in mind that the lifetime of the string is limited by the lifetime of the handle
       * from which it was obtained. If you need to keep the string longer, make a copy,
       * or for example store it in a std::string.
       * Error checking for simple getters is usually not needed, we keep it for consistency. */
      auto displayName = gsDiscoveredCameraGetDisplayName(discoveredCamera, &hErr);
      throwOnError(hErr);

      std::cout << "\t" << displayName << std::endl;
    }

    /* Before trying to open, we can check the access status of the (first discovered) device.
     * If the status is "NoAccess", most likely a wrong IP address (not matching the subnet of the network card
     * it is connected to).
     * We can demonstrate how a suitable IP address can be "forced" into the device, however, configuring
     * the device to boot into expected IP subnet is recommended instead. */
    DiscoveredCamera discoveredCamera;
    gsDiscoveryGetDiscoveredCamera(discovery, 0, discoveredCamera.ptr(), &hErr);
    throwOnError(hErr);
    auto accessStatus = gsDiscoveredCameraGetAccessStatus(discoveredCamera, &hErr);
    throwOnError(hErr);
    if (accessStatus == egsAccessStatusNoAccess)
    {
      std::cout << "Attempting to force suitable IP address into the (currently unreachable) device "
                << gsDiscoveredCameraGetDisplayName(discoveredCamera, GsIgnoreErrorInfo) << std::endl;

      /* The SICK GenTL Producer allows to propose and force a suitable temporary IP address to the device
       * and GenIStreamC wraps that functionality in an easy to use interface. */
      uint32_t proposedIpAddress = 0;
      uint32_t proposedSubnetMask = 0;
      gsDiscoveryProposeIp(discovery, discoveredCamera, discoveryTimeoutMs, &proposedIpAddress, &proposedSubnetMask,
                           &hErr);
      throwOnError(hErr);
      String proposedIpAddressStr;
      gsIpAddressToString(proposedIpAddress, proposedIpAddressStr.ptr(), GsIgnoreErrorInfo);
      String proposedSubnetMaskStr;
      gsIpAddressToString(proposedSubnetMask, proposedSubnetMaskStr.ptr(), GsIgnoreErrorInfo);
      std::cout << "\tThe proposed IP address is " << proposedIpAddressStr.str() << ", subnet mask "
                << proposedSubnetMaskStr.str() << ", forcing it into the device" << std::endl;

      /* Let GenIStreamC to "force" the camera to use the proposed IP address.
       * IMPORTANT: the forced IP address is just a temporary one and will be lost after the next device power cycle. */
      gsDiscoveryForceIp(discovery, discoveredCamera, proposedIpAddress, proposedSubnetMask, discoveryTimeoutMs, &hErr);
      throwOnError(hErr);

      /* Update the device list again to get a fresh state including the device access mode reflecting the new IP
       * address. */
      gsDiscoveryScanForCameras(discovery, discoveryTimeoutMs, &hErr);
      throwOnError(hErr);
      numCameras = gsDiscoveryGetNumDiscoveredCameras(discovery, &hErr);
      throwOnError(hErr);
      if (numCameras == 0)
      {
        std::cerr << "Failed to re-discover the camera after the force-IP procedure" << std::endl;
        return 1;
      }
      /* Release the previous discovered camera handle and reuse the wrapper to get its re-discovered instance. */
      discoveredCamera.release();
      gsDiscoveryGetDiscoveredCamera(discovery, 0, discoveredCamera.ptr(), &hErr);
      throwOnError(hErr);
    }

    /* If the access status is still not "ReadWrite" (meaning it is ready to open) or if we failed for any reason to
     * rediscover the camera, stop the example. This can happen because of multiple reasons, including that the camera
     * is already open by another application and is beyond the scope of this example. (Also beyond this example's scope
     * is the situation of multiple cameras connected to the system and discovered by GenIStreamC always in different
     * order - in such cases identifying the cameras by MAC or serial number would be better than simply taking the
     * first seen one as in this test). */
    accessStatus = gsDiscoveredCameraGetAccessStatus(discoveredCamera, &hErr);
    throwOnError(hErr);
    if (accessStatus != egsAccessStatusReadWrite)
    {
      std::cerr << "The discovered camera is not ready to open" << std::endl;
      return 1;
    }

    /* Connect to the camera to be able to configure it and acquire images. After a successful connection our example
     * will "own" the camera and no other application will be able to connect. For the reasons described also above, the
     * call can still fail if another application managed to connect in the meanwhile. Same comments apply about use of
     * the use of shared_ptr to maintain ownership of the camera object (and keeping the connection open through it).
     * Side note: after this point the discoveredCamera handle is no more used and could also be released if desired. */
    Camera camera;
    gsDiscoveryConnectToCamera(discovery, discoveredCamera, camera.ptr(), &hErr);
    throwOnError(hErr);
    /* Now we can get access to the device parameters - GenICam map of feature nodes defining user interface of the
     * device. We can use these features to configure the device properties. For details about individual available
     * parameters, their data types, limits and mutual relationships, refer to the camera feature documentation
     * delivered together with this package. */
    Parameters parameters;
    gsCameraCreateParameters(camera, parameters.ptr(), &hErr);
    throwOnError(hErr);

    /* Enable both acquired image components (Range data and RGB intensity data) to be delivered with the stream.
     * IMPORTANT: at least one of the components should always be enabled. While the camera itself allows streaming
     * chunk data only, the current version of GenIStreamC is not (yet) ready for such mode. */
    gsParametersSetEnum(parameters, "ComponentSelector", "Range", &hErr);
    throwOnError(hErr);
    gsParametersSetBool(parameters, "ComponentEnable", GsTrue, &hErr);
    throwOnError(hErr);
    gsParametersSetEnum(parameters, "ComponentSelector", "Intensity", &hErr);
    throwOnError(hErr);
    gsParametersSetBool(parameters, "ComponentEnable", GsTrue, &hErr);
    throwOnError(hErr);
    /* The basic IMU data is not needed / used in this sample. Hence, we can disable this component.
     * Note however that the IMU component is not implemented by all models in the Visionary camera family,
     * therefore we should check its presence before trying to use it (to keep this example generic for all
     * Visionary camera models).
     * The access mode related checks are implemented as pure getters in GenIStreamC, returning directly
     * the GsBool value (GsFalse fallback in case of any problem), thus we can skip the error checking safely. */
    if (GsIsTrue(gsParametersIsImplementedEnumEntry(parameters, "ComponentSelector", "ImuBasic", GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(parameters, "ComponentSelector", "ImuBasic", &hErr);
      throwOnError(hErr);
      gsParametersSetBool(parameters, "ComponentEnable", GsFalse, &hErr);
      throwOnError(hErr);
    }
    /* Ensure the "chunk data" is switched on (chunk data is again a GenICam term for metadata transferred in the
     * stream), for the Visionary devices it especially carries the camera intrinsic parameters required to build the
     * point cloud. */
    gsParametersSetBool(parameters, "ChunkModeActive", GsTrue, &hErr);
    throwOnError(hErr);
    /* For purpose of this test, set a lower frame rate and insert an "inter-packet delay" to slow the stream and make
     * this example reliable in all setups - note that to reach good results (mainly no lost packets) with full-speed
     * stream, the receiver should be running on a well performing and optimized system. Most importantly the network
     * card should be configured to allow receiving larger packets ("jumbo frames"). Find more info in the camera
     * documentation. Note also that with small negotiated packet size (implying many packets required to transfer each
     * frame) and following (relatively large) inter-packet delay, the camera might be producing frames faster then
     * their packets can be sent out, resulting in dropped frames on camera side.
     * Please note also that to be able to reliably configure frame rate, we are first switching the auto-exposure
     * feature "off" (frame rate feature would not be available otherwise). This is an example of a feature
     * dependency to be considered (mentioned in camera features documentation). Because the camera keeps its
     * configuration across multiple connections, one should keep in mind that when freshly connected, the camera
     * is in the state where it was left during the previous session.
     * As one more complication, the ExposureAuto feature is not supported on all Visionary camera models, therefore we
     * need to check if given GenICam feature is implemented before accessing it (to keep this generic for all camera
     * models). Note that you could use similar checks also for other feature-access-mode checks such as whether given
     * feature is currently readable or writable - might come handy when accessing features depending on current camera
     * state. */
    if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureAuto", GsIgnoreErrorInfo)))
    {
      /* ExposureAuto functionality is currently only implemented for the Range exposure on Visionary cameras.
       * The topic of separate exposure configuration for range and exposure sensors on some cameras is further
       * discussed below. */
      if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureTimeSelector", GsIgnoreErrorInfo)))
      {
        gsParametersSetEnum(parameters, "ExposureTimeSelector", "Range", &hErr);
        throwOnError(hErr);
      }

      gsParametersSetEnum(parameters, "ExposureAuto", "Off", &hErr);
      throwOnError(hErr);
    }
    gsParametersSetFloat(parameters, "AcquisitionFrameRate", 5.0, &hErr); /* (in Hz) */
    throwOnError(hErr);
    gsParametersSetInt(parameters, "GevSCPD", 100000, &hErr); /* (packet delay, in ns) */
    throwOnError(hErr);

    /* Refer to the camera documentation to learn about other parameters, such as ExposureAuto, MultiSlopeMode (HDR...)
     * or Scan3dDataFilterSelector (ValidationFilter) which can have significant impact on the output quality,
     * but depend on the actual scene and conditions.
     * Assuming that this tutorial is usually executed in developer's "office conditions" (indoor, low light levels),
     * thus we attempt below to set few more parameters, trying to achieve reasonable output quality in such conditions.
     * It should be understood, however, that this selection is sensitive to actual environment and you might need
     * to adjust them if the output is not good enough.
     * In particular, we disable auto-exposure (already done above), set long enough exposure time and apply strongest
     * possible validation filter level to eliminate most unwanted outliers (in the range data). The actual suitable
     * exposure time value strongly depends on your working conditions and the camera model, please adjust it as needed
     * when getting poor output results.
     * One more camera specific note: if currently tested camera models supports configuring the exposure time
     * separately for the range and intensity image sensor, we should specify which one we mean. Note once again the
     * checks for availability of features which are implemented only in a subset of the Visionary camera models. When
     * targeting a concrete model with known set of supported features, you could skip those checks. */
    if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureTimeSelector", GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(parameters, "ExposureTimeSelector", "Range", &hErr);
      throwOnError(hErr);
    }
    if (GsIsTrue(gsParametersIsImplemented(parameters, "MultiSlopeMode", GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(parameters, "MultiSlopeMode", "Off", &hErr);
      throwOnError(hErr);
    }
    if (GsIsTrue(gsParametersIsImplemented(parameters, "ExposureTime", GsIgnoreErrorInfo)))
    {
      double maxExpTime = 0.0;
      gsParametersGetFloatMax(parameters, "ExposureTime", &maxExpTime, &hErr);
      throwOnError(hErr);
      gsParametersSetFloat(parameters, "ExposureTime", std::min(25000.0, maxExpTime), &hErr);
      throwOnError(hErr);
    }
    if (GsIsTrue(gsParametersIsImplementedEnumEntry(parameters, "Scan3dDataFilterSelector", "ValidationFilter",
                                                    GsIgnoreErrorInfo)))
    {
      gsParametersSetEnum(parameters, "Scan3dDataFilterSelector", "ValidationFilter", &hErr);
      throwOnError(hErr);
      gsParametersSetBool(parameters, "Scan3dDataFilterEnable", GsTrue, &hErr);
      throwOnError(hErr);
      gsParametersSetInt(parameters, "Scan3dDepthValidationFilterLevel", -3, &hErr);
      throwOnError(hErr);
    }

    /* The GenTL Producer will attempt to negotiate with the camera highest possible packet size supported by all the
     * involved network components. If the negotiated packet size is not bigger than the standard 1500B, likely the
     * above mentioned jumbo frame support was not enabled on the network card or the camera is connected through a
     * switch without jumbo frame support. For the purpose of this tests, let's just issue a warning, for real world use
     * this should be addressed properly. */
    int64_t packetSize = 0;
    gsParametersGetInt(parameters, "GevSCPSPacketSize", &packetSize, &hErr);
    throwOnError(hErr);
    std::cout << "Negotiated packet size: " << packetSize << std::endl;
    /* The only exception is the Visionary-T Mini camera model, which does not support jumbo frames itself
     * and reports max supported packet size value to be 1500 - issue the warning only for models supporting
     * larger packet sizes. The Visionary-T Mini data rate is not that high and packet size not that important. */
    int64_t maxPacketSize = 0;
    gsParametersGetIntMax(parameters, "GevSCPSPacketSize", &maxPacketSize, &hErr);
    throwOnError(hErr);
    if (packetSize <= 1500 && maxPacketSize > 1500)
    {
      std::cout << "BEWARE: failed to negotiate larger packet size (>1500B), enable jumbo frame support on the network "
                   "card for optimal performance"
                << std::endl;
    }

    /* Finally, before starting acquisition, it is usually useful to increase the default number of buffers GenIStream
     * will use for acquisition - this can help overcome possible temporary hiccups in the buffer processing on
     * GenIStream/application side. */
    const size_t numBuffers = 10;
    /* Instantiate the "grabber" object we'll be using to control the acquisition. */
    FrameGrabber grabber;
    gsCameraCreateFrameGrabber(camera, numBuffers, grabber.ptr(), &hErr);
    throwOnError(hErr);

    /* Now we can start the acquisition. Note that during active acquisition many of the camera parameters will be
     * locked (not writable). */
    gsFrameGrabberStart(grabber, &hErr);
    throwOnError(hErr);

    /* To demonstrate the acquisition loop, let's just fetch few buffers in a loop. */
    auto const imagesToAcquire = 20;
    std::cout << "Going to acquire " << imagesToAcquire << " frames" << std::endl;
    for (auto iNumAcquired = 0; iNumAcquired < imagesToAcquire; /* incremented inside the loop */)
    {
      /* The gsFrameGrabberGrabNext() function lets the "grabber" to acquire a new buffer/image
       * and provide higher level information about the result (as we'll see below).
       * It also controls the buffer ownership - as soon as the frame object (GsFrameH handle or its
       * C++ wrapper) is released, the buffer is automatically returned to the acquisition engine for refill
       * - it is important to release the frame objects as soon as you're done with given buffer.
       * When acquiring in a loop and using the helper RAII handle wrappers (such as in this example),
       * the object is naturally released when going out of scope at the end of each loop. */
      GrabResult grabResult;
      gsFrameGrabberGrabNext(grabber, grabResult.ptr(), &hErr);
      throwOnError(hErr);

      /* First check if the operation was successful. Besides a successfully acquired frame, the result could
       * also contain information about a failure (timeout, aborted) or information about lost frames.
       * See detailed documentation of gsGrabResultHasFrame() and its sibling functions for further details. */
      if (GsIsTrue(gsGrabResultHasFrame(grabResult, GsIgnoreErrorInfo)))
      {
        /* Received a good frame, print its frame ID. Skipped IDs signal any kind of acquisition problem:
         * dropped frame(s) (due to corrupted/lost packets), queue overflow on camera side (cannot send all required
         * packets possibly due to too long packet delay) or queue "underflow" in the receiver side. Most of these cases
         * usually signal performance problems on the receiver and/or its network stack. */
        Frame frame;
        /* (GsFalse means we don't want exceptions thrown for incomplete frames) */
        gsGrabResultGetFrame(grabResult, GsFalse, frame.ptr(), &hErr);
        throwOnError(hErr);
        auto frameId = gsFrameGetId(frame, GsIgnoreErrorInfo);
        std::cout << "\tAcquired frame ID " << frameId << std::endl;
        ++iNumAcquired;
      }
      else if (GsIsTrue(gsGrabResultHasLostFrames(grabResult, GsIgnoreErrorInfo)))
      {
        /* This grab-result only informs us about detected lost frames (possible reasons described above).
         * Just print the information and continue - next valid frame should be delivered through the next
         * gsFrameGrabberGrabNext() call. */
        auto lostFrameCount = gsGrabResultGetLostFrameCount(grabResult, GsIgnoreErrorInfo);
        std::cout << "\tEncountered " << lostFrameCount << " lost frames" << std::endl;
      }
      else
      {
        /* Other cases mean either the acquisition was aborted (e.g. if it was stopped from another thread) or other
         * kind of grab failure - in this simple example let's just exit in such case. */
        std::cerr << "Failed to acquire the frame(s) in a loop" << std::endl;
        return 1;
      }
    }

    /* First simple acquisition loop test is finished, stop the acquisition. */
    gsFrameGrabberStop(grabber, &hErr);
    throwOnError(hErr);

    /* In case of suspecting acquisition performance issues, we can print some statistics, which are provided by the
     * GenTL Producer itself. GenIStream provides access to all of them at once using the following call. */
    Statistics statistics;
    gsCameraGetDataStreamStatistics(camera, statistics.ptr(), &hErr);
    throwOnError(hErr);
    std::cout << "Selected stream statistics" << std::endl;
    std::cout << "\tNumber of blocks skipped (for any reason, camera or receiver side): "
              << gsStatisticsSkippedBlockCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of discarded blocks (typically because they arrived corrupted with too big packet loss): "
              << gsStatisticsDiscardedBlockCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of blocks that were delivered, but were missing some data packets: "
              << gsStatisticsIncompleteBlockCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout
        << "\tNumber of blocks discarded by the acquisition if no acquisition buffers were free to fill at the moment: "
        << gsStatisticsEngineUnderrunCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of all stream packets seen by the acquisition engine: "
              << gsStatisticsSeenPacketCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of all packets detected as lost by the engine: "
              << gsStatisticsLostPacketCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of all packets in successfully delivered frames: "
              << gsStatisticsDeliveredPacketCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of packet resend request commands issued by the acquisition engine: "
              << gsStatisticsResendCommandCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of all packets requested in those resend request commands: "
              << gsStatisticsResendPacketCount(statistics, GsIgnoreErrorInfo) << std::endl;
    std::cout << "\tNumber of resend-requested packets which were marked as no more available by the camera: "
              << gsStatisticsUnavailablePacketCount(statistics, GsIgnoreErrorInfo) << std::endl;

    /* Here the acquisition is stopped and we could have chance to reconfigure again any camera parameters that are
     * locked during acquisition. Instead, we simply start the acquisition again and acquire just a single buffer to
     * demonstrate how to query and process its contents. */
    gsFrameGrabberStart(grabber, &hErr);
    throwOnError(hErr);
    /* (Similar code to grab and validate the result as above...) */
    GrabResult grabResult;
    gsFrameGrabberGrabNext(grabber, grabResult.ptr(), &hErr);
    throwOnError(hErr);
    if (GsIsTrue(gsGrabResultHasLostFrames(grabResult, GsIgnoreErrorInfo)))
    {
      /* (release and reuse the grab-result handle wrapper) */
      grabResult.release();
      gsFrameGrabberGrabNext(grabber, grabResult.ptr(), &hErr);
      throwOnError(hErr);
    }
    if (GsIsFalse(gsGrabResultHasFrame(grabResult, GsIgnoreErrorInfo)))
    {
      std::cerr << "Failed to acquire the frame to build the point cloud" << std::endl;
      return 1;
    }
    Frame frame;
    gsGrabResultGetFrame(grabResult, GsFalse, frame.ptr(), &hErr);
    throwOnError(hErr);
    std::cout << "Acquired one more frame to demonstrate buffer contents processing" << std::endl;
    if (GsIsTrue(gsFrameIsIncomplete(frame, GsIgnoreErrorInfo)))
    {
      std::cout << "\tBeware though, the frame was acquired incomplete (one or more packets lost)" << std::endl;
    }

    /* The acquired buffer will contain one or more "components". Because we have enabled "Range" and "Intensity"
     * components when configuring the device above, those two should always be delivered. The GenIStream grabber
     * interface will automatically identify and deliver those components to us.
     * IMPORTANT: for Visionary cameras the other component types (e.g. "reflectance") offered by the frame
     * interface are not relevant (those are used by other camera types).
     * Beware: the calls will fail if given components are not present in the acquired frame (e.g. if you did not
     * enable them in the configuration phase). */
    Component cmpRange;
    gsFrameGetRange(frame, cmpRange.ptr(), &hErr);
    throwOnError(hErr);
    Component cmpIntensity;
    gsFrameGetIntensity(frame, cmpIntensity.ptr(), &hErr);
    throwOnError(hErr);

    /* Print basic information about the two acquired components.
     * Note however, that for the initial releases of the Visionary cameras, the size and pixel format
     * of the components is usually fixed - with the exception of component size on models supporting
     * the binning features. */
    auto intensityW = gsComponentGetWidth(cmpIntensity, GsIgnoreErrorInfo);
    auto intensityH = gsComponentGetDeliveredHeight(cmpIntensity, GsIgnoreErrorInfo);
    auto intensityFmt = gsComponentGetPixelFormat(cmpIntensity, GsIgnoreErrorInfo);
    /* To identify the pixel formats based on the GenICam PFNC standard, we use the identifiers from the official
     * PFNC header file, which also contains additional utilities like GetPixelFormatName() providing
     * additional information for every standard format. */
    std::cout << "The frame contains intensity component, size: " << intensityW << "x" << intensityH
              << ", pixel format " << GetPixelFormatName(PfncFormat(intensityFmt)) << std::endl;
    /* The intensity data for Visionary models are expected either in BGR8 format (each pixel consisting
     * of three uint8 values, B-G-R) or in Mono16 format (each pixel being a single uint16 value),
     * depending on the actual camera model you are testing with. */
    if (intensityFmt != PFNC_BGR8 && intensityFmt != PFNC_Mono16)
    {
      std::cerr << "\tUnexpected pixel format for intensity data, BGR8 or Mono16 data expected" << std::endl;
      return 1;
    }
    auto const* intensityData = gsComponentGetData(cmpIntensity, GsIgnoreErrorInfo);

    /* Similar for range */
    auto rangeW = gsComponentGetWidth(cmpRange, GsIgnoreErrorInfo);
    auto rangeH = gsComponentGetDeliveredHeight(cmpRange, GsIgnoreErrorInfo);
    auto rangeFmt = gsComponentGetPixelFormat(cmpRange, GsIgnoreErrorInfo);
    std::cout << "The frame contains range component, size: " << rangeW << "x" << rangeH << ", pixel format "
              << ", pixel format " << GetPixelFormatName(PfncFormat(rangeFmt)) << std::endl;
    /* The range data are expected in COORD_3D_C16 format for Visionary - pixels carrying uint16 values,
     * hence the cast to utilize per-pixel access. */
    if (rangeFmt != PFNC_Coord3D_C16)
    {
      std::cerr << "\tUnexpected pixel format for range data, Coord3D_C16 data expected" << std::endl;
      return 1;
    }
    auto const* rangeData = reinterpret_cast<const uint16_t*>(gsComponentGetData(cmpRange, GsIgnoreErrorInfo));

    /* Besides accessing the actual acquired data of range and intensity components, to compute the point cloud
     * real world coordinates, we'll need the corresponding parameters (camera intrinsics) to compute it.
     * These are delivered, as usual in GigE Vision & GenICam world, within the stream "per buffer" metadata,
     * called "chunk data" in GenICam. Note that we have enabled delivery of the chunk data during the device
     * configuration above. GenICam standard provides means to access the chunk data through the same "node map"
     * (accessible over the GsParametersH interface in GenIStreamC) as the device configuration features - and
     * GenIStreamC library ensures that the metadata (if delivered with the buffer) is connected to the node map while
     * you are working with an acquired GsFrameH object. Note however, that the chunk features always refer to the LAST
     * successfully grabbed frame (and before grabbing the first frame, no chunk data will be available). Note that the
     * Visionary device intrinsics will stay the same for all buffers during the acquisition session, but it is in
     * general good practice to read the chunk data per buffer, because for other chunk parameters or other device types
     * they might actually change frame by frame. Note that attempt to read the chunk data features when no buffer is
     * fetched or when the chunk data was not delivered will fail. */

    /* Read the important camera intrinsics
     * (refer to GenICam SFNC standard and PointCloudGeneration.pdf for detailed explanations). */
    double focalLength = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dFocalLength", &focalLength, &hErr);
    throwOnError(hErr);
    double aspectRatio = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dAspectRatio", &aspectRatio, &hErr);
    throwOnError(hErr);
    double princPtU = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dPrincipalPointU", &princPtU, &hErr);
    throwOnError(hErr);
    double princPtV = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dPrincipalPointV", &princPtV, &hErr);
    throwOnError(hErr);
    /* Read also the scale / offset to be applied on the "coordinate C" (acquired range value). */
    gsParametersSetEnum(parameters, "ChunkScan3dCoordinateSelector", "CoordinateC", &hErr);
    throwOnError(hErr);
    double coordCScale = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateScale", &coordCScale, &hErr);
    throwOnError(hErr);
    double coordCOffset = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateOffset", &coordCOffset, &hErr);
    throwOnError(hErr);
    /* Read information about the range value used to flag an invalid pixel (carrying no useful measurement). */
    GsBool invalidFlagUsed = GsFalse;
    gsParametersGetBool(parameters, "ChunkScan3dInvalidDataFlag", &invalidFlagUsed, &hErr);
    throwOnError(hErr);
    double invalidFlagValue = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dInvalidDataValue", &invalidFlagValue, &hErr);
    throwOnError(hErr);
    /* For completeness, read also the 3D output mode defining the algorithm to build point cloud from the acquired
     * range ("depth") data - although we know that Visionary devices always use the "ProjectedC" mode as described in
     * PointCloudGeneration.pdf and illustrated in the example code below. */
    String outputMode;
    gsParametersGetEnum(parameters, "ChunkScan3dOutputMode", outputMode.ptr(), &hErr);
    throwOnError(hErr);
    /* And finally the parameters of the optional anchor-to-reference coordinate system transformation.
     * (Details again in PointCloudGeneration.pdf and especially in the GenICam SFNC document). */
    gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "RotationX", &hErr);
    throwOnError(hErr);
    double refCoordRotX = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordRotX, &hErr);
    throwOnError(hErr);
    gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "RotationY", &hErr);
    throwOnError(hErr);
    double refCoordRotY = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordRotY, &hErr);
    throwOnError(hErr);
    gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "RotationZ", &hErr);
    throwOnError(hErr);
    double refCoordRotZ = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordRotZ, &hErr);
    throwOnError(hErr);
    gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "TranslationX", &hErr);
    throwOnError(hErr);
    double refCoordTransX = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordTransX, &hErr);
    throwOnError(hErr);
    gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "TranslationY", &hErr);
    throwOnError(hErr);
    double refCoordTransY = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordTransY, &hErr);
    throwOnError(hErr);
    gsParametersSetEnum(parameters, "ChunkScan3dCoordinateReferenceSelector", "TranslationZ", &hErr);
    throwOnError(hErr);
    double refCoordTransZ = 0.0;
    gsParametersGetFloat(parameters, "ChunkScan3dCoordinateReferenceValue", &refCoordTransZ, &hErr);
    throwOnError(hErr);

    /* Print the values. */
    std::cout << "Received stream meta data ('chunk data') values:" << std::endl;
    std::cout << "\tOutput mode: " << outputMode.str() << std::endl;
    std::cout << "\tFocal length (in pixels): " << focalLength << std::endl;
    std::cout << "\tAspect ratio: " << aspectRatio << std::endl;
    std::cout << "\tPrincipal point [U,V]: [" << princPtU << ", " << princPtV << "]" << std::endl;
    std::cout << "\tCoordinate C (range) scale: " << coordCScale << std::endl;
    std::cout << "\tCoordinate C (range) offset: " << coordCOffset << std::endl;
    std::cout << "\tInvalid data flag used: " << GsIsTrue(invalidFlagUsed) << std::endl;
    std::cout << "\tInvalid data flag value: " << invalidFlagValue << std::endl;
    std::cout << "\tOptional reference coordinate transformation parameters:" << std::endl;
    std::cout << "\t\tRotation X (in degrees): " << refCoordRotX << std::endl;
    std::cout << "\t\tRotation Y (in degrees): " << refCoordRotY << std::endl;
    std::cout << "\t\tRotation Z (in degrees): " << refCoordRotZ << std::endl;
    std::cout << "\t\tTranslation X (in mm): " << refCoordTransX << std::endl;
    std::cout << "\t\tTranslation Y (in mm): " << refCoordTransY << std::endl;
    std::cout << "\t\tTranslation Z (in mm): " << refCoordTransZ << std::endl;

    /* Let's store the two acquired components as images to the disk to demonstrate GenIStreamC data access.
     * See also the code above querying the component properties - however, for now we can rely also on the fact
     * that the pixel format used by the image components (intensity and range) is always the same
     * on Visionary cameras (it might not be the case in future firmware releases). */

    /* Storing the data in BMP format simply because the format is trivial - the export is done using a tiny
     * SimpleBMP library (see helpers subdirectory delivered with the example).
     * SimpleBMP assumes the data in BGR 8-bit format - prepare the data in that shape depending on the actual
     * output format used by the camera under test (remember that can be either BGR8 or Mono16, see above). */
    std::vector<uint8_t> monoAsBGR; /* (converted data if required) */
    const uint8_t* intensityDataBGR;
    if (intensityFmt == PFNC_BGR8)
    {
      /* The camera outputs BGR8 which is exactly what we need to for SimpleBMP output. */
      intensityDataBGR = reinterpret_cast<const uint8_t*>(intensityData);
    }
    else
    {
      /* The other alternative is Mono16. */
      assert(intensityFmt == PFNC_Mono16);
      monoAsBGR = bgr8FromMono16(intensityData, intensityW, intensityH);
      intensityDataBGR = monoAsBGR.data();
    }
    assert(intensityDataBGR != nullptr);
    /* Finally store it to the disk. */
    SimpleBMP::save(static_cast<int>(intensityW), static_cast<int>(intensityH), intensityDataBGR, "intensity.bmp");

    /* The range is delivered as single channel 16-bit data, so we need to build what SimpleBMP needs.
     * Note that this modified image is not intended for further processing, the only goal is to make it easily
     * viewable. */
    auto rangeAsBGR = bgr8FromMono16(rangeData, rangeW, rangeH);
    SimpleBMP::save(static_cast<int>(rangeW), static_cast<int>(rangeH), rangeAsBGR.data(), "range.bmp");

    /* Finally, let's also demonstrate, how a point cloud with all three world coordinates could be computed from the
     * acquired range data. The algorithm is described in PointCloudGeneration.pdf and mostly relies on principles &
     * intrinsic parameters in GenICam SFNC standard - only the new ChunkScan3dOutputMode "ProjectedC" is currently
     * still in ratification process, scheduled to be released with next official SFNC version.
     * The following lines aim just to demonstrate that algorithm in code - depending on your actual needs or use
     * of the data you might wish to adjust it for your needs. */

    /* To be able to review the result, we are going to store the computed point cloud to the disk in the .ply format,
     * using the hapPLY library (see again the helpers subdirectory). The point data are therefore being stored
     * in the data structures expected by the hapPLY library. */
    std::vector<std::array<double, 3>> pointCoordinates;
    std::vector<std::array<unsigned char, 3>> pointColors;
    /* On current Visionary camera models the dimensions of the range and intensity components are either exactly equal,
     * or the intensity component is twice as big (also depending on binning configuration on models supporting
     * binning). This needs to be considered when overlaying the intensity "colors" on the point cloud (range values).
     * For now let's just rely on this - the approach might get even a bit more complicated if in future some of the
     * cameras support other size-affecting features such as cropping. On the other hand, on cameras where the range
     * and intensity image are always equal sized (such as on Visionary-B Two), the scaling topic can be ignored. */
    auto const maxPoints = rangeW * rangeH;
    pointCoordinates.reserve(maxPoints);
    pointColors.reserve(maxPoints);
    auto const intensityScaleX = intensityW / rangeW;
    auto const intensityScaleY = intensityH / rangeH;
    /* Calculating the point coordinates pixel by pixel. Focusing on clear demonstration of the algorithm, not on
     * performance and user app might organize/use the points differently. */
    for (auto row = 0u; row < rangeH; ++row)
    {
      for (auto col = 0u; col < rangeW; ++col)
      {
        auto const rangePixelOffset = row * rangeW + col;
        /* For simplicity we'll pick directly a single intensity pixel for overlay even if the intensity is scaled,
         * more advanced approach could involve some kind of interpolation. */
        auto const intensityPixelOffset = row * intensityScaleY * intensityW + col * intensityScaleX;

        /* Raw range-coordinate value delivered by the camera for given pixe. */
        auto coordCValue = rangeData[rangePixelOffset];

        /* Remember that not all pixels might carry a valid measurement - the invalid ones might be marked using the
         * corresponding invalid data flag. The actual use and value of the flag was read together with the intrinsics
         * from chunk data above. Current version of the Visionary cameras always switch use of the flag ON (true) and
         * the flag value is fixed at 0. Zero delivered range pixels therefore denote invalid pixels - let's filter them
         * out (skip them) of the point cloud output. The following lines anyway rely on the generically retrieved
         * invalid value parameters. */
        if (invalidFlagUsed && invalidFlagValue == coordCValue)
        {
          continue;
        }

        /* The X / Y coordinate per - pixel multiplicators
         * (Note: these can be also precomputed just once within the acquisition loop, relying on the fact that the
         * intrinsic parameters will not change during the acquisition). */
        auto xp = (col - princPtU) / focalLength;
        auto yp = (row - princPtV) / (focalLength * aspectRatio);

        /* The actual coordinate values xc / yc / zc for each pixel are all computed from the measured range (depth)
         * values. The distance units used by Visionary are millimeters (could also be "formally" queried from
         * ChunkScan3dDistanceUnit). */
        auto scaledC = coordCValue * coordCScale + coordCOffset;
        auto xc = xp * scaledC;
        auto yc = yp * scaledC;
        auto zc = scaledC;
        pointCoordinates.push_back({xc, yc, zc});

        /* Note that the computed coordinates are in camera's native "Anchor" coordinate system which is related to its
         * internal geometry and might be affected by current operating mode and sensor mounting deviations. Compare
         * this to the "Reference" coordinate system with origin in the center of camera's front surface and Z pointing
         * out of the camera. When conversion to the Reference coordinate system is required, it can be performed here
         * using the transformation parameters queried above, in following order: 1. refCoordRotX, 2. refCoordRotY, 3.
         * refCoordRotZ, 4. all translations. */

        /* Store also the R-G-B channel values for this pixel to allow adding the intensity-color overlay to the ouptut
         * PLY file. */
        auto b = intensityDataBGR[3 * intensityPixelOffset];
        auto g = intensityDataBGR[3 * intensityPixelOffset + 1];
        auto r = intensityDataBGR[3 * intensityPixelOffset + 2];
        pointColors.push_back({r, g, b});
      }
    }
    /* Let's just print trivial statistics about the computed point cloud. */
    std::cout << "Number of valid points: " << pointCoordinates.size() << std::endl;
    /* Store the collected point cloud data to a file. */
    happly::PLYData ply;
    ply.addVertexPositions(pointCoordinates);
    ply.addVertexColors(pointColors);
    ply.write("pointcloud.ply", happly::DataFormat::Binary);

    /* Stop the acquisition once finished the processing. */
    gsFrameGrabberStop(grabber, &hErr);
    throwOnError(hErr);

    /* Finally, let's also demonstrate the asynchronous events functionality.
     * Because the initial firmware versions did not support events, we need to check,
     * whether our test camera has events already implemented.
     * All event-enabled cameras have to support the "Test" event type and provide
     * the "TestEventGenerate" feature to reliably generate it, we will therefore
     * use this event in this example.
     * Any other events supported by the camera would be handled the very same way,
     * just note that they might need to be enabled using the EventSelector and
     * EventNotification features, example:
     * params->setEnum("EventSelector", "OperationStatusChange");
     * params->setEnum("EventNotification", "On"); */
    if (GsIsTrue(gsParametersIsImplemented(parameters, "EventTest", GsIgnoreErrorInfo)))
    {
      /* The "canonical" way to handle GenICam events is described in GenICam SFNC document
       * (refer there for more details if desired).
       * To listen for an event X, the application should install an invalidation callback
       * on corresponding event-identifier feature EventX. When the event arrives, the callback
       * will be fired and the application can read other data related to that event.
       * Every event carries a timestamp (integer feature EventXTimestamp), but some events
       * can also carry additional data (refer to camera documentation about individual events).
       * Note that the callback should do only minimal required actions and return quickly.
       * Beware also that the callback will be fired from the event handling thread (beware
       * of eventual thread safety issues in your code).
       * Finally, we're testing with the "Test" event, therefore X will be replaced with Test
       * in the above mentioned names. */

      /* Prepare the callback - for simplicity just a small non-capturing lambda which can be converted
       * to the function pointer we need.
       * In our case, we use the callback only to monitor the single parameter (EventTest),
       * therefore we can ignore its parameterName parameter.
       * All we do in the callback is to print info about the received event and the timestamp
       * attached to it. Note that resolution of Visionary timestamp counter is in nanosecond
       * units.
       * We will use the "user context" parameter to provide access to the camera parameters handle into the callback
       * (once again - we cannot capture it into the lambda so that it can be converted to the function pointer).
       * Various other ways to implement the callback would of course be possible. */
      auto testEventCallback = [](const char* /*pParamName*/, void* pContext) {
        auto parameters = reinterpret_cast<GsParametersH>(pContext);
        int64_t eventTimestamp = 0;
        if (GsOK(gsParametersGetInt(parameters, "EventTestTimestamp", &eventTimestamp, GsIgnoreErrorInfo)))
        {
          std::cout << "\t...received test event with timestamp: " << eventTimestamp << std::endl;
        }
        else
        {
          std::cout << "\t...received test event, failed to read its timestamp" << std::endl;
        }
      };

      /* Attach the callback to the EventTest feature.
       * IMPORTANT: the subscription instance need to stay valid as long as we are interested in
       * the events - once it is released, the callback gets unsubscribed automatically. */
      Subscription testEventSubscription;
      auto testEventCallbackFptr = static_cast<GsNodeInvalidationCallback>(testEventCallback);
      auto testEventContext = parameters.get();
      gsParametersSubscribeToInvalidation(parameters, "EventTest", testEventCallbackFptr, testEventContext,
                                          testEventSubscription.ptr(), &hErr);
      throwOnError(hErr);

      /* Now everything is ready, instruct the camera to generate the test event for us
       * and wait a bit until it arrives. */
      std::cout << "Asking the device to generate a test event..." << std::endl;
      gsParametersExecuteCommand(parameters, "TestEventGenerate", GsTrue, &hErr);
      throwOnError(hErr);
      std::this_thread::sleep_for(std::chrono::seconds(1));

      /* Repeat the same thing once again to have more fun and to observe that the other event
       * will carry different timestamp. */
      std::cout << "Asking the device to generate another test event..." << std::endl;
      gsParametersExecuteCommand(parameters, "TestEventGenerate", GsTrue, &hErr);
      throwOnError(hErr);
      std::this_thread::sleep_for(std::chrono::seconds(1));

      /* Other "real" events supported by the camera would be handled in very similar way,
       * just the code triggered by their arrival would match the purpose of each respective
       * event type. */
    }
  }
  /* Specific fine-granular exception handling skipped in this example to keep the code simple,
   * real application might wish to add exception handling close to the possible exception source
   * depending on the actual application logic. */
  catch (std::runtime_error& e)
  {
    std::cerr << "A GenIStreamC call failed within the example code: " << std::endl;
    std::cerr << e.what() << std::endl;
    return 1;
  }

  return 0;
}

/* Trivial ad-hoc converter transforming 16-bit monochrome image data to a BGR8 format
 * required for the color image output use above in the example. */
static std::vector<uint8_t> bgr8FromMono16(void const* data, size_t width, size_t height)
{
  /* The data is assumed to carry 16-bit monochrome pixels. */
  uint16_t const* mono16Data = static_cast<uint16_t const*>(data);

  /* If the 16-bit input is range, depending on the field of view, the actual measured range values can be rather
   * small for the 16-bit range. Similar if the 16-bit input is intensity captured with in low light conditions.
   * Let's roughly scale the data to full range and convert to uint8 dtype before saving (for human eye friendliness).
   * Note that this modified image is not intended for further processing, the only goal is to make it easily
   * viewable.
   * IMPORTANT: this crude brightness adjustment is added here just for convenience when viewing the tutorial
   * outputs, it is NOT assumed to be used in a real application. */
  auto maxDataValue = *std::max_element(mono16Data, mono16Data + width * height);
  auto u8Factor = maxDataValue / 255 + 1;
  std::vector<uint8_t> dataAsBGR(width * height * 3);
  for (size_t i = 0; i < width * height; ++i)
  {
    auto grayVal = static_cast<uint8_t>(mono16Data[i] / u8Factor);
    dataAsBGR[3 * i] = grayVal;
    dataAsBGR[3 * i + 1] = grayVal;
    dataAsBGR[3 * i + 2] = grayVal;
  }

  return dataAsBGR;
}
