/* Copyright (c) 2015-2025 The Khronos Group Inc.
 * Copyright (c) 2015-2025 Valve Corporation
 * Copyright (c) 2015-2025 LunarG, Inc.
 * Copyright (C) 2015-2024 Google Inc.
 * Modifications Copyright (C) 2020-2022 Advanced Micro Devices, Inc. All rights reserved.
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

#include <string>

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/utility/vk_format_utils.h>
#include "error_message/error_location.h"
#include "core_validation.h"
#include "state_tracker/buffer_state.h"
#include "state_tracker/cmd_buffer_state.h"
#include "generated/dispatch_functions.h"
#include "utils/math_utils.h"
#include "utils/image_utils.h"

// Helper function to validate usage flags for buffers. For given buffer_state send actual vs. desired usage off to helper above
// where an error will be flagged if usage is not correct
bool CoreChecks::ValidateBufferUsageFlags(const LogObjectList &objlist, vvl::Buffer const &buffer_state,
                                          VkBufferUsageFlags2 desired, bool strict, const char *vuid,
                                          const Location &buffer_loc) const {
    bool skip = false;
    bool correct_usage = false;
    if (strict) {
        correct_usage = ((buffer_state.usage & desired) == desired);
    } else {
        correct_usage = ((buffer_state.usage & desired) != 0);
    }

    if (!correct_usage) {
        skip |= LogError(vuid, objlist, buffer_loc, "(%s) was created with %s but requires %s.",
                         FormatHandle(buffer_state.Handle()).c_str(), string_VkBufferUsageFlags2(buffer_state.usage).c_str(),
                         string_VkBufferUsageFlags2(desired).c_str());
    }
    return skip;
}

bool CoreChecks::ValidateBufferViewRange(const vvl::Buffer &buffer_state, const VkBufferViewCreateInfo &create_info,
                                         const Location &loc) const {
    bool skip = false;

    const VkDeviceSize &range = create_info.range;
    if (range != VK_WHOLE_SIZE) {
        // The sum of range and offset must be less than or equal to the size of buffer
        if (range + create_info.offset > buffer_state.create_info.size) {
            skip |= LogError("VUID-VkBufferViewCreateInfo-offset-00931", buffer_state.Handle(), loc.dot(Field::range),
                             "(%" PRIuLEAST64 ") does not equal VK_WHOLE_SIZE, the sum of offset (%" PRIuLEAST64
                             ") and range must be less than or equal to the size of the buffer (%" PRIuLEAST64 ").",
                             range, create_info.offset, buffer_state.create_info.size);
        }
    } else {
        const VkFormat format = create_info.format;
        // will be 1 because  block-compressed format are not supported for Texe l Buffer
        const VkDeviceSize texels_per_block = static_cast<VkDeviceSize>(vkuFormatTexelsPerBlock(format));
        const VkDeviceSize texel_block_size = static_cast<VkDeviceSize>(GetTexelBufferFormatSize(format));
        const VkDeviceSize offset_range = buffer_state.create_info.size - create_info.offset;
        const VkDeviceSize texels = SafeDivision(offset_range, texel_block_size) * texels_per_block;
        if (texels > static_cast<VkDeviceSize>(phys_dev_props.limits.maxTexelBufferElements)) {
            skip |= LogError("VUID-VkBufferViewCreateInfo-range-04059", buffer_state.Handle(), loc.dot(Field::range),
                             "is VK_WHOLE_SIZE, but the buffer's size (%" PRIuLEAST64 "), offset (%" PRIuLEAST64
                             "), %s texel block size (%" PRIuLEAST64 "), and texels per block (%" PRIuLEAST64
                             ") is a total of (%" PRIuLEAST64
                             ") texels which is more than VkPhysicalDeviceLimits::maxTexelBufferElements (%" PRIuLEAST32 ").",
                             buffer_state.create_info.size, create_info.offset, string_VkFormat(format), texel_block_size,
                             texels_per_block, texels, phys_dev_props.limits.maxTexelBufferElements);
        }
    }
    return skip;
}

bool CoreChecks::ValidateCreateBufferDescriptorBuffer(const VkBufferCreateInfo &create_info, const VkBufferUsageFlags2 &usage,
                                                      const Location &create_info_loc) const {
    bool skip = false;

    if (usage & VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT) {
        if (create_info.size + device_state->samplerDescriptorBufferAddressSpaceSize >
            phys_dev_ext_props.descriptor_buffer_props.samplerDescriptorBufferAddressSpaceSize) {
            skip |= LogError(
                "VUID-VkBufferCreateInfo-usage-08097", device, create_info_loc.dot(Field::size),
                "(%" PRIuLEAST64 ") + current total (%" PRIuLEAST64
                ") is greater than specified in properties field samplerDescriptorBufferAddressSpaceSize (%" PRIuLEAST64 ").",
                create_info.size, device_state->samplerDescriptorBufferAddressSpaceSize.load(),
                phys_dev_ext_props.descriptor_buffer_props.samplerDescriptorBufferAddressSpaceSize);
        } else if (create_info.size + device_state->descriptorBufferAddressSpaceSize >
                   phys_dev_ext_props.descriptor_buffer_props.descriptorBufferAddressSpaceSize) {
            skip |= LogError("VUID-VkBufferCreateInfo-usage-08097", device, create_info_loc.dot(Field::size),
                             "(%" PRIuLEAST64 ") + current total (%" PRIuLEAST64
                             ") is greater than specified in properties field descriptorBufferAddressSpaceSize (%" PRIuLEAST64 ")",
                             create_info.size, device_state->descriptorBufferAddressSpaceSize.load(),
                             phys_dev_ext_props.descriptor_buffer_props.descriptorBufferAddressSpaceSize);
        }
    }

    if (usage & VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT) {
        if (create_info.size + device_state->resourceDescriptorBufferAddressSpaceSize >
            phys_dev_ext_props.descriptor_buffer_props.resourceDescriptorBufferAddressSpaceSize) {
            skip |= LogError(
                "VUID-VkBufferCreateInfo-usage-08098", device, create_info_loc.dot(Field::size),
                "(%" PRIuLEAST64 ") + current total (%" PRIuLEAST64
                ") is greater than specified in properties field resourceDescriptorBufferAddressSpaceSize (%" PRIuLEAST64 ").",
                create_info.size, device_state->resourceDescriptorBufferAddressSpaceSize.load(),
                phys_dev_ext_props.descriptor_buffer_props.resourceDescriptorBufferAddressSpaceSize);
        } else if (create_info.size + device_state->descriptorBufferAddressSpaceSize >
                   phys_dev_ext_props.descriptor_buffer_props.descriptorBufferAddressSpaceSize) {
            skip |= LogError("VUID-VkBufferCreateInfo-usage-08098", device, create_info_loc.dot(Field::size),
                             "(%" PRIuLEAST64 ") + current total (%" PRIuLEAST64
                             ") is greater than specified in properties field descriptorBufferAddressSpaceSize (%" PRIuLEAST64 ").",
                             create_info.size, device_state->descriptorBufferAddressSpaceSize.load(),
                             phys_dev_ext_props.descriptor_buffer_props.descriptorBufferAddressSpaceSize);
        }
    }

    if ((create_info.flags & VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT) &&
        !enabled_features.descriptorBufferCaptureReplay) {
        skip |= LogError("VUID-VkBufferCreateInfo-flags-08099", device, create_info_loc.dot(Field::flags),
                         "has VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT set but the descriptorBufferCaptureReplay "
                         "device feature is not enabled.");
    }

    if (vku::FindStructInPNextChain<VkOpaqueCaptureDescriptorDataCreateInfoEXT>(create_info.pNext)) {
        if (!(create_info.flags & VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
            skip |= LogError("VUID-VkBufferCreateInfo-pNext-08100", device, create_info_loc.dot(Field::flags),
                             "(%s) is missing VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT but "
                             "VkOpaqueCaptureDescriptorDataCreateInfoEXT is in pNext chain.\n%s",
                             string_VkBufferCreateFlags(create_info.flags).c_str(),
                             PrintPNextChain(Struct::VkBufferCreateInfo, create_info.pNext).c_str());
        }
    }

    if (usage & VK_BUFFER_USAGE_2_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT) {
        if (!enabled_features.descriptorBufferPushDescriptors) {
            skip |= LogError("VUID-VkBufferCreateInfo-usage-08101", device, create_info_loc.dot(Field::usage),
                             "has VK_BUFFER_USAGE_2_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT set but the "
                             "descriptorBufferPushDescriptors device feature is not enabled.");
        }

        if (phys_dev_ext_props.descriptor_buffer_props.bufferlessPushDescriptors) {
            skip |=
                LogError("VUID-VkBufferCreateInfo-usage-08102", device, create_info_loc.dot(Field::usage),
                         "has VK_BUFFER_USAGE_2_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT set but the bufferlessPushDescriptors "
                         "device feature is enabled.");
        }

        if (!(usage &
              (VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT))) {
            skip |= LogError("VUID-VkBufferCreateInfo-usage-08103", device, create_info_loc.dot(Field::usage), "is (%s).",
                             string_VkBufferUsageFlags2(usage).c_str());
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
                                             const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer,
                                             const ErrorObject &error_obj) const {
    bool skip = false;
    skip |= ValidateDeviceQueueSupport(error_obj.location);
    const Location create_info_loc = error_obj.location.dot(Field::pCreateInfo);

    if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT && pCreateInfo->pQueueFamilyIndices) {
        skip |= ValidatePhysicalDeviceQueueFamilies(pCreateInfo->queueFamilyIndexCount, pCreateInfo->pQueueFamilyIndices,
                                                    create_info_loc, "VUID-VkBufferCreateInfo-sharingMode-01419");
    }

    const auto *usage_flags2 = vku::FindStructInPNextChain<VkBufferUsageFlags2CreateInfo>(pCreateInfo->pNext);
    const VkBufferUsageFlags2 usage = usage_flags2 ? usage_flags2->usage : pCreateInfo->usage;

    const bool has_decode_usage = usage & (VK_BUFFER_USAGE_2_VIDEO_DECODE_SRC_BIT_KHR | VK_BUFFER_USAGE_2_VIDEO_DECODE_DST_BIT_KHR);
    const bool has_encode_usage = usage & (VK_BUFFER_USAGE_2_VIDEO_ENCODE_SRC_BIT_KHR | VK_BUFFER_USAGE_2_VIDEO_ENCODE_DST_BIT_KHR);
    const bool video_profile_independent = pCreateInfo->flags & VK_BUFFER_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;

    if (video_profile_independent && !enabled_features.videoMaintenance1) {
        skip |= LogError("VUID-VkBufferCreateInfo-flags-08325", device, create_info_loc.dot(Field::flags),
                         "has VK_BUFFER_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR set but the videoMaintenance1 "
                         "device feature is not enabled.");
    }

    if (has_decode_usage || has_encode_usage) {
        const bool expect_decode_profile = has_decode_usage && !video_profile_independent;
        const bool expect_encode_profile = has_encode_usage && !video_profile_independent;

        const auto *video_profiles = vku::FindStructInPNextChain<VkVideoProfileListInfoKHR>(pCreateInfo->pNext);
        skip |= core::ValidateVideoProfileListInfo(
            *this, video_profiles, error_obj, create_info_loc.pNext(Struct::VkVideoProfileListInfoKHR), expect_decode_profile,
            "VUID-VkBufferCreateInfo-usage-04813", expect_encode_profile, "VUID-VkBufferCreateInfo-usage-04814");
    }

    auto external_memory_info = vku::FindStructInPNextChain<VkExternalMemoryBufferCreateInfo>(pCreateInfo->pNext);
    if (external_memory_info && external_memory_info->handleTypes) {
        const uint32_t any_type = 1u << MostSignificantBit(external_memory_info->handleTypes);
        VkPhysicalDeviceExternalBufferInfo external_buffer_info = vku::InitStructHelper();
        external_buffer_info.flags = pCreateInfo->flags;
        // for now no VkBufferUsageFlags2 flag can be used, so safe to pass in as 32-bit version
        external_buffer_info.usage = VkBufferUsageFlags(pCreateInfo->usage);
        external_buffer_info.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(any_type);
        VkExternalBufferProperties external_buffer_properties = vku::InitStructHelper();
        DispatchGetPhysicalDeviceExternalBufferPropertiesHelper(api_version, physical_device, &external_buffer_info,
                                                                &external_buffer_properties);
        const auto compatible_types = external_buffer_properties.externalMemoryProperties.compatibleHandleTypes;

        if ((external_memory_info->handleTypes & compatible_types) != external_memory_info->handleTypes) {
            skip |= LogError("VUID-VkBufferCreateInfo-pNext-00920", device,
                             create_info_loc.pNext(Struct::VkExternalMemoryBufferCreateInfo, Field::handleTypes),
                             "(%s) is not reported as compatible by vkGetPhysicalDeviceExternalBufferProperties.",
                             string_VkExternalMemoryHandleTypeFlags(external_memory_info->handleTypes).c_str());
        }
    }

    skip |= ValidateCreateBufferDescriptorBuffer(*pCreateInfo, usage, create_info_loc);

    return skip;
}

bool CoreChecks::PreCallValidateCreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
                                                 const VkAllocationCallbacks *pAllocator, VkBufferView *pView,
                                                 const ErrorObject &error_obj) const {
    bool skip = false;
    skip |= ValidateDeviceQueueSupport(error_obj.location);
    auto buffer_state_ptr = Get<vvl::Buffer>(pCreateInfo->buffer);
    const Location create_info_loc = error_obj.location.dot(Field::pCreateInfo);
    // If this isn't a sparse buffer, it needs to have memory backing it at CreateBufferView time
    if (!buffer_state_ptr) return skip;

    const auto &buffer_state = *buffer_state_ptr;
    const LogObjectList objlist(device, pCreateInfo->buffer);

    skip |= ValidateMemoryIsBoundToBuffer(device, buffer_state, create_info_loc.dot(Field::buffer),
                                          "VUID-VkBufferViewCreateInfo-buffer-00935");
    // In order to create a valid buffer view, the buffer must have been created with at least one of the following flags:
    // UNIFORM_TEXEL_BUFFER_BIT or STORAGE_TEXEL_BUFFER_BIT
    skip |= ValidateBufferUsageFlags(objlist, buffer_state,
                                     VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT, false,
                                     "VUID-VkBufferViewCreateInfo-buffer-00932", create_info_loc.dot(Field::buffer));

    // Buffer view offset must be less than the size of buffer
    if (pCreateInfo->offset >= buffer_state.create_info.size) {
        skip |= LogError("VUID-VkBufferViewCreateInfo-offset-00925", buffer_state.Handle(), create_info_loc.dot(Field::offset),
                         "(%" PRIuLEAST64 ") must be less than the size of the buffer (%" PRIuLEAST64 ").", pCreateInfo->offset,
                         buffer_state.create_info.size);
    }

    if (enabled_features.texelBufferAlignment) {
        VkDeviceSize texel_block_size = GetTexelBufferFormatSize(pCreateInfo->format);
        if ((texel_block_size % 3) == 0) {
            texel_block_size /= 3;
        }
        if (buffer_state.usage & VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT) {
            VkDeviceSize alignment_requirement = phys_dev_props_core13.storageTexelBufferOffsetAlignmentBytes;
            if (phys_dev_props_core13.storageTexelBufferOffsetSingleTexelAlignment) {
                alignment_requirement = std::min(alignment_requirement, texel_block_size);
            }
            if (SafeModulo(pCreateInfo->offset, alignment_requirement) != 0) {
                skip |= LogError("VUID-VkBufferViewCreateInfo-buffer-02750", objlist, create_info_loc.dot(Field::buffer),
                                 "was created with VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT, so the offset (%" PRIuLEAST64
                                 ") must be a multiple of the lesser of "
                                 "storageTexelBufferOffsetAlignmentBytes (%" PRIuLEAST64
                                 ") or, if storageTexelBufferOffsetSingleTexelAlignment "
                                 "(%" PRId32
                                 ") is VK_TRUE, the size of a texel of the requested format. "
                                 "If the size of a texel is a multiple of three bytes, then the size of a "
                                 "single component of format is used instead",
                                 pCreateInfo->offset, phys_dev_props_core13.storageTexelBufferOffsetAlignmentBytes,
                                 phys_dev_props_core13.storageTexelBufferOffsetSingleTexelAlignment);
            }
        }
        if (buffer_state.usage & VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT) {
            VkDeviceSize alignment_requirement = phys_dev_props_core13.uniformTexelBufferOffsetAlignmentBytes;
            if (phys_dev_props_core13.uniformTexelBufferOffsetSingleTexelAlignment) {
                alignment_requirement = std::min(alignment_requirement, texel_block_size);
            }
            if (SafeModulo(pCreateInfo->offset, alignment_requirement) != 0) {
                skip |= LogError("VUID-VkBufferViewCreateInfo-buffer-02751", objlist, create_info_loc.dot(Field::buffer),
                                 "was created with VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT, so the offset (%" PRIuLEAST64
                                 ") must be a multiple of the lesser of "
                                 "uniformTexelBufferOffsetAlignmentBytes (%" PRIuLEAST64
                                 ") or, if uniformTexelBufferOffsetSingleTexelAlignment "
                                 "(%" PRId32
                                 ") is VK_TRUE, the size of a texel of the requested format. "
                                 "If the size of a texel is a multiple of three bytes, then the size of a "
                                 "single component of format is used instead",
                                 pCreateInfo->offset, phys_dev_props_core13.uniformTexelBufferOffsetAlignmentBytes,
                                 phys_dev_props_core13.uniformTexelBufferOffsetSingleTexelAlignment);
            }
        }
    }

    // vkspec.html#resources-buffer-views-usage
    auto usage_flags2 = vku::FindStructInPNextChain<VkBufferUsageFlags2CreateInfo>(pCreateInfo->pNext);
    VkBufferUsageFlags2 buffer_view_usage = usage_flags2 ? usage_flags2->usage : buffer_state.usage;

    if (usage_flags2) {
        if ((buffer_view_usage & ~(VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT)) != 0) {
            skip |= LogError("VUID-VkBufferViewCreateInfo-pNext-08780", objlist,
                             create_info_loc.pNext(Struct::VkBufferUsageFlags2CreateInfo, Field::usage), "is %s.",
                             string_VkBufferUsageFlags2(buffer_view_usage).c_str());
        } else if ((buffer_view_usage & buffer_state.usage) != buffer_view_usage) {
            skip |=
                LogError("VUID-VkBufferViewCreateInfo-pNext-08781", objlist,
                         create_info_loc.pNext(Struct::VkBufferUsageFlags2CreateInfo, Field::usage),
                         "(%s) is not a subset of the buffer's usage (%s).", string_VkBufferUsageFlags2(buffer_view_usage).c_str(),
                         string_VkBufferUsageFlags2(buffer_state.usage).c_str());
        }
    }

    const VkFormatProperties3KHR format_properties = GetPDFormatProperties(pCreateInfo->format);
    if ((buffer_view_usage & VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT) &&
        !(format_properties.bufferFeatures & VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT)) {
        const Location usage_loc = usage_flags2 ? create_info_loc.pNext(Struct::VkBufferUsageFlags2CreateInfo, Field::usage)
                                                : create_info_loc.dot(Field::buffer);
        skip |=
            LogError("VUID-VkBufferViewCreateInfo-format-08778", objlist, usage_loc,
                     "%s(%s) containing VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT.\n"
                     "Format (%s) doesn't support VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT.\n"
                     "(supported bufferFeatures: %s)",
                     usage_flags2 ? "" : "was created with usage ", string_VkBufferUsageFlags2(buffer_view_usage).c_str(),
                     string_VkFormat(pCreateInfo->format), string_VkFormatFeatureFlags2(format_properties.bufferFeatures).c_str());
    }
    if ((buffer_view_usage & VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT) &&
        !(format_properties.bufferFeatures & VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT)) {
        const Location usage_loc = usage_flags2 ? create_info_loc.pNext(Struct::VkBufferUsageFlags2CreateInfo, Field::usage)
                                                : create_info_loc.dot(Field::buffer);
        skip |=
            LogError("VUID-VkBufferViewCreateInfo-format-08779", objlist, usage_loc,
                     "%s(%s) containing VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT.\n"
                     "Format (%s) doesn't support VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT.\n"
                     "(supported bufferFeatures: %s)",
                     usage_flags2 ? "" : "was created with usage ", string_VkBufferUsageFlags2(buffer_view_usage).c_str(),
                     string_VkFormat(pCreateInfo->format), string_VkFormatFeatureFlags2(format_properties.bufferFeatures).c_str());
    }

    skip |= ValidateBufferViewRange(buffer_state, *pCreateInfo, create_info_loc);

    return skip;
}

bool CoreChecks::PreCallValidateDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator,
                                              const ErrorObject &error_obj) const {
    bool skip = false;
    if (auto buffer_state = Get<vvl::Buffer>(buffer)) {
        skip |= ValidateObjectNotInUse(buffer_state.get(), error_obj.location, "VUID-vkDestroyBuffer-buffer-00922");
    }
    return skip;
}

bool CoreChecks::PreCallValidateDestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator,
                                                  const ErrorObject &error_obj) const {
    bool skip = false;
    if (auto buffer_view_state = Get<vvl::BufferView>(bufferView)) {
        skip |= ValidateObjectNotInUse(buffer_view_state.get(), error_obj.location, "VUID-vkDestroyBufferView-bufferView-00936");
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                              VkDeviceSize size, uint32_t data, const ErrorObject &error_obj) const {
    bool skip = false;
    auto cb_state_ptr = GetRead<vvl::CommandBuffer>(commandBuffer);
    auto buffer_state = Get<vvl::Buffer>(dstBuffer);
    ASSERT_AND_RETURN_SKIP(cb_state_ptr && buffer_state);

    const LogObjectList objlist(commandBuffer, dstBuffer);
    const vvl::CommandBuffer &cb_state = *cb_state_ptr;
    const Location buffer_loc = error_obj.location.dot(Field::dstBuffer);
    skip |= ValidateMemoryIsBoundToBuffer(commandBuffer, *buffer_state, buffer_loc, "VUID-vkCmdFillBuffer-dstBuffer-00031");
    skip |= ValidateCmd(cb_state, error_obj.location);
    // Validate that DST buffer has correct usage flags set
    skip |= ValidateBufferUsageFlags(objlist, *buffer_state, VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, true,
                                     "VUID-vkCmdFillBuffer-dstBuffer-00029", buffer_loc);

    skip |= ValidateProtectedBuffer(cb_state, *buffer_state, buffer_loc, "VUID-vkCmdFillBuffer-commandBuffer-01811");
    skip |= ValidateUnprotectedBuffer(cb_state, *buffer_state, buffer_loc, "VUID-vkCmdFillBuffer-commandBuffer-01812");

    if (dstOffset >= buffer_state->create_info.size) {
        skip |= LogError("VUID-vkCmdFillBuffer-dstOffset-00024", objlist, error_obj.location.dot(Field::dstOffset),
                         "(%" PRIu64 ") is not less than destination buffer (%s) size (%" PRIu64 ").", dstOffset,
                         FormatHandle(dstBuffer).c_str(), buffer_state->create_info.size);
    }

    if ((size != VK_WHOLE_SIZE) && (size > (buffer_state->create_info.size - dstOffset))) {
        skip |= LogError("VUID-vkCmdFillBuffer-size-00027", objlist, error_obj.location.dot(Field::size),
                         "(%" PRIu64 ") is greater than dstBuffer (%s) size (%" PRIu64 ") minus dstOffset (%" PRIu64 ").", size,
                         FormatHandle(dstBuffer).c_str(), buffer_state->create_info.size, dstOffset);
    }

    if (!IsExtEnabled(extensions.vk_khr_maintenance1)) {
        const VkQueueFlags required_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if (!HasRequiredQueueFlags(cb_state, *physical_device_state, required_flags)) {
            const LogObjectList objlist_pool(cb_state.Handle(), cb_state.command_pool->Handle());
            skip |= LogError("VUID-vkCmdFillBuffer-apiVersion-07894", objlist_pool, error_obj.location, "%s",
                             DescribeRequiredQueueFlag(cb_state, *physical_device_state, required_flags).c_str());
        }
    }

    return skip;
}

// Validates the buffer is allowed to be protected
bool CoreChecks::ValidateProtectedBuffer(const vvl::CommandBuffer &cb_state, const vvl::Buffer &buffer_state,
                                         const Location &buffer_loc, const char *vuid, const char *more_message) const {
    bool skip = false;

    // if driver supports protectedNoFault the operation is valid, just has undefined values
    if ((!phys_dev_props_core11.protectedNoFault) && (cb_state.unprotected == true) && (buffer_state.unprotected == false)) {
        const LogObjectList objlist(cb_state.Handle(), buffer_state.Handle());
        skip |= LogError(vuid, objlist, buffer_loc, "(%s) is a protected buffer, but command buffer (%s) is unprotected.%s",
                         FormatHandle(buffer_state).c_str(), FormatHandle(cb_state).c_str(), more_message);
    }
    return skip;
}

// Validates the buffer is allowed to be unprotected
bool CoreChecks::ValidateUnprotectedBuffer(const vvl::CommandBuffer &cb_state, const vvl::Buffer &buffer_state,
                                           const Location &buffer_loc, const char *vuid, const char *more_message) const {
    bool skip = false;

    // if driver supports protectedNoFault the operation is valid, just has undefined values
    if ((!phys_dev_props_core11.protectedNoFault) && (cb_state.unprotected == false) && (buffer_state.unprotected == true)) {
        const LogObjectList objlist(cb_state.Handle(), buffer_state.Handle());
        skip |= LogError(vuid, objlist, buffer_loc, "(%s) is an unprotected buffer, but command buffer (%s) is protected.%s",
                         FormatHandle(buffer_state).c_str(), FormatHandle(cb_state).c_str(), more_message);
    }
    return skip;
}
