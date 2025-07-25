/* Copyright (c) 2015-2025 The Khronos Group Inc.
 * Copyright (c) 2015-2025 Valve Corporation
 * Copyright (c) 2015-2025 LunarG, Inc.
 * Modifications Copyright (C) 2020 Advanced Micro Devices, Inc. All rights reserved.
 * Modifications Copyright (C) 2022 RasterGrid Kft.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "best_practices/best_practices_validation.h"
#include "best_practices/bp_state.h"
#include "generated/error_location_helper.h"
#include "state_tracker/device_state.h"
#include "state_tracker/wsi_state.h"

bool bp_state::Instance::ValidateGetPhysicalDeviceDisplayPlanePropertiesKHRQuery(VkPhysicalDevice physicalDevice,
                                                                                 const Location& loc) const {
    bool skip = false;
    if (const auto bp_pd_state = Get<vvl::PhysicalDevice>(physicalDevice)) {
        if (bp_pd_state->WasUncalled(vvl::Func::vkGetPhysicalDeviceDisplayPlanePropertiesKHR) &&
            bp_pd_state->WasUncalled(vvl::Func::vkGetPhysicalDeviceDisplayPlaneProperties2KHR)) {
            skip |= LogWarning("BestPractices-vkGetDisplayPlaneSupportedDisplaysKHR-properties-not-retrieved", physicalDevice, loc,
                               "was called without first retrieving properties from "
                               "vkGetPhysicalDeviceDisplayPlanePropertiesKHR or vkGetPhysicalDeviceDisplayPlaneProperties2KHR.");
        }
    }

    return skip;
}

bool bp_state::Instance::PreCallValidateGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex,
                                                                            uint32_t* pDisplayCount, VkDisplayKHR* pDisplays,
                                                                            const ErrorObject& error_obj) const {
    bool skip = false;

    skip |= ValidateGetPhysicalDeviceDisplayPlanePropertiesKHRQuery(physicalDevice, error_obj.location);

    return skip;
}

bool bp_state::Instance::PreCallValidateGetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode,
                                                                       uint32_t planeIndex,
                                                                       VkDisplayPlaneCapabilitiesKHR* pCapabilities,
                                                                       const ErrorObject& error_obj) const {
    bool skip = false;

    skip |= ValidateGetPhysicalDeviceDisplayPlanePropertiesKHRQuery(physicalDevice, error_obj.location);

    return skip;
}

bool bp_state::Instance::PreCallValidateGetDisplayPlaneCapabilities2KHR(VkPhysicalDevice physicalDevice,
                                                                        const VkDisplayPlaneInfo2KHR* pDisplayPlaneInfo,
                                                                        VkDisplayPlaneCapabilities2KHR* pCapabilities,
                                                                        const ErrorObject& error_obj) const {
    bool skip = false;

    skip |= ValidateGetPhysicalDeviceDisplayPlanePropertiesKHRQuery(physicalDevice, error_obj.location);

    return skip;
}

bool BestPractices::PreCallValidateCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                      const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain,
                                                      const ErrorObject& error_obj) const {
    bool skip = false;

    if (physical_device_state->WasUncalled(vvl::Func::vkGetPhysicalDeviceSurfaceCapabilitiesKHR) &&
        physical_device_state->WasUncalled(vvl::Func::vkGetPhysicalDeviceSurfaceCapabilities2EXT) &&
        physical_device_state->WasUncalled(vvl::Func::vkGetPhysicalDeviceSurfaceCapabilities2KHR)) {
        skip |= LogWarning("BestPractices-vkCreateSwapchainKHR-capabilities-no-surface", device, error_obj.location,
                           "called before getting surface capabilities from "
                           "vkGetPhysicalDeviceSurfaceCapabilitiesKHR or vkGetPhysicalDeviceSurfaceCapabilities2EXT or "
                           "vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    }

    if (pCreateInfo->presentMode != VK_PRESENT_MODE_FIFO_KHR &&
        physical_device_state->GetCallState(vvl::Func::vkGetPhysicalDeviceSurfacePresentModesKHR) != vvl::CallState::QueryDetails) {
        skip |= LogWarning("BestPractices-vkCreateSwapchainKHR-present-mode-no-surface", device, error_obj.location,
                           "called before getting surface present mode(s) from "
                           "vkGetPhysicalDeviceSurfacePresentModesKHR.");
    }

    if (physical_device_state->GetCallState(vvl::Func::vkGetPhysicalDeviceSurfaceFormatsKHR) != vvl::CallState::QueryDetails &&
        physical_device_state->GetCallState(vvl::Func::vkGetPhysicalDeviceSurfaceFormats2KHR) != vvl::CallState::QueryDetails) {
        skip |= LogWarning("BestPractices-vkCreateSwapchainKHR-formats-no-surface", device, error_obj.location,
                           "called before getting surface format(s) from vkGetPhysicalDeviceSurfaceFormatsKHR or "
                           "vkGetPhysicalDeviceSurfaceFormats2KHR.");
    }

    if ((pCreateInfo->queueFamilyIndexCount > 1) && (pCreateInfo->imageSharingMode == VK_SHARING_MODE_EXCLUSIVE)) {
        skip |= LogWarning("BestPractices-vkCreateSwapchainKHR-sharing-mode-exclusive", device, error_obj.location,
                           "A Swapchain is being created which specifies a sharing mode of VK_SHARING_MODE_EXCLUSIVE while "
                           "specifying multiple queues (queueFamilyIndexCount of %" PRIu32 ").",
                           pCreateInfo->queueFamilyIndexCount);
    }

    const auto present_mode = pCreateInfo->presentMode;
    if (((present_mode == VK_PRESENT_MODE_MAILBOX_KHR) || (present_mode == VK_PRESENT_MODE_FIFO_KHR)) &&
        (pCreateInfo->minImageCount == 2)) {
        skip |= LogPerformanceWarning(
            "BestPractices-vkCreateSwapchainKHR-suboptimal-swapchain-image-count", device, error_obj.location,
            "A Swapchain is being created with minImageCount set to %" PRIu32
            ", which means double buffering is going "
            "to be used. Using double buffering and vsync locks rendering to an integer fraction of the vsync rate. In turn, "
            "reducing the performance of the application if rendering is slower than vsync. Consider setting minImageCount to "
            "3 to use triple buffering to maximize performance in such cases.",
            pCreateInfo->minImageCount);
    }

    if ((IsExtEnabled(extensions.vk_khr_swapchain_maintenance1) || IsExtEnabled(extensions.vk_ext_swapchain_maintenance1)) &&
        !vku::FindStructInPNextChain<VkSwapchainPresentModesCreateInfoKHR>(pCreateInfo->pNext)) {
        skip |= LogWarning("BestPractices-vkCreateSwapchainKHR-no-VkSwapchainPresentModesCreateInfoKHR-provided", device,
                           error_obj.location,
                           "No VkSwapchainPresentModesCreateInfoKHR was provided to VkCreateSwapchainKHR. "
                           "When VK_KHR_swapchain_maintenance1 is enabled, a VkSwapchainPresentModesCreateInfoKHR should "
                           "be provided to inform the implementation that the application is aware of the new features "
                           "in a backward compatible way.");
    }

    if (VendorCheckEnabled(kBPVendorArm) && (pCreateInfo->presentMode != VK_PRESENT_MODE_FIFO_KHR)) {
        skip |= LogWarning("BestPractices-Arm-vkCreateSwapchainKHR-swapchain-presentmode-not-fifo", device, error_obj.location,
                           "%s Swapchain is not being created with presentation mode \"VK_PRESENT_MODE_FIFO_KHR\". "
                           "Prefer using \"VK_PRESENT_MODE_FIFO_KHR\" to avoid unnecessary CPU and GPU load and save power. "
                           "Presentation modes which are not FIFO will present the latest available frame and discard other "
                           "frame(s) if any.",
                           VendorSpecificTag(kBPVendorArm));
    }

    return skip;
}

bool BestPractices::PreCallValidateCreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount,
                                                             const VkSwapchainCreateInfoKHR* pCreateInfos,
                                                             const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains,
                                                             const ErrorObject& error_obj) const {
    bool skip = false;

    for (uint32_t i = 0; i < swapchainCount; i++) {
        if ((pCreateInfos[i].queueFamilyIndexCount > 1) && (pCreateInfos[i].imageSharingMode == VK_SHARING_MODE_EXCLUSIVE)) {
            skip |= LogWarning("BestPractices-vkCreateSharedSwapchainsKHR-sharing-mode-exclusive", device,
                               error_obj.location.dot(Field::pCreateInfos, i),
                               "A shared swapchain is being created which specifies a sharing mode of VK_SHARING_MODE_EXCLUSIVE "
                               "while specifying multiple "
                               "queues (queueFamilyIndexCount of %" PRIu32 ").",
                               pCreateInfos[i].queueFamilyIndexCount);
        }
    }

    return skip;
}

void BestPractices::ManualPostCallRecordQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo,
                                                        const RecordObject& record_obj) {
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        auto swapchains_result = pPresentInfo->pResults ? pPresentInfo->pResults[i] : record_obj.result;
        if (swapchains_result == VK_SUBOPTIMAL_KHR) {
            LogPerformanceWarning(
                "BestPractices-vkCreateSharedSwapchainsKHR-SuboptimalSwapchain", pPresentInfo->pSwapchains[i],
                record_obj.location.dot(Field::pPresentInfo, i),
                "VK_SUBOPTIMAL_KHR was returned. VK_SUBOPTIMAL_KHR - Presentation will still succeed, "
                "subject to the window resize behavior, but the swapchain (%s) is no longer configured optimally for the surface "
                "it targets.\nApplications should query updated surface information and recreate their swapchain at the next "
                "convenient opportunity.",
                FormatHandle(pPresentInfo->pSwapchains[i]).c_str());
        }
    }

    // AMD best practice
    // end-of-frame cleanup
    num_queue_submissions_ = 0;
    num_barriers_objects_ = 0;
    ClearPipelinesUsedInFrame();
}

bool bp_state::Instance::PreCallValidateGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                                           uint32_t* pSurfaceFormatCount,
                                                                           VkSurfaceFormatKHR* pSurfaceFormats,
                                                                           const ErrorObject& error_obj) const {
    bool skip = false;
    const auto bp_pd_state = Get<vvl::PhysicalDevice>(physicalDevice);
    if (!bp_pd_state || !pSurfaceFormats) return skip;

    if (pSurfaceFormatCount && *pSurfaceFormatCount > bp_pd_state->surface_formats_count) {
        skip |=
            LogWarning("BestPractices-GetPhysicalDeviceSurfaceFormatsKHR-CountMismatch", physicalDevice,
                       error_obj.location.dot(Field::pSurfaceFormatCount),
                       "(%" PRIu32 ") is greater than the value (%" PRIu32 ") that was returned when pSurfaceFormatCount was NULL.",
                       *pSurfaceFormatCount, bp_pd_state->surface_formats_count);
    }
    return skip;
}

bool BestPractices::PreCallValidateQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo,
                                                   const ErrorObject& error_obj) const {
    bool skip = false;

    if (VendorCheckEnabled(kBPVendorAMD) || VendorCheckEnabled(kBPVendorNVIDIA)) {
        auto num = num_queue_submissions_.load();
        if (num > kNumberOfSubmissionWarningLimitAMD) {
            skip |= LogPerformanceWarning("BestPractices-Submission-ReduceNumberOfSubmissions", device, error_obj.location,
                                          "%s %s command buffers submitted %" PRId32
                                          " times this frame. Submitting command buffers has a CPU "
                                          "and GPU overhead. Submit fewer times to incur less overhead.",
                                          VendorSpecificTag(kBPVendorAMD), VendorSpecificTag(kBPVendorNVIDIA), num);
        }
    }

    return skip;
}

bool BestPractices::PreCallValidateAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                                       VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex,
                                                       const ErrorObject& error_obj) const {
    auto swapchain_data = Get<vvl::Swapchain>(swapchain);
    bool skip = false;
    if (swapchain_data && swapchain_data->images.empty()) {
        skip |= LogWarning("BestPractices-vkAcquireNextImageKHR-SwapchainImagesNotFound", swapchain, error_obj.location,
                           "No images found to acquire from. Application probably did not call "
                           "vkGetSwapchainImagesKHR after swapchain creation.");
    }
    return skip;
}
