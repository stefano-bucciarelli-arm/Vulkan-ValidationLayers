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
#include "state_tracker/render_pass_state.h"
#include "utils/assert_utils.h"

bool BestPractices::ClearAttachmentsIsFullClear(const bp_state::CommandBufferSubState& cb_state, uint32_t rectCount,
                                                const VkClearRect* pRects) const {
    if (cb_state.base.IsSecondary()) {
        // We don't know the accurate render area in a secondary,
        // so assume we clear the entire frame buffer.
        // This is resolved in CmdExecuteCommands where we can check if the clear is a full clear.
        return true;
    }

    // If we have a rect which covers the entire frame buffer, we have a LOAD_OP_CLEAR-like command.
    for (uint32_t i = 0; i < rectCount; i++) {
        auto& rect = pRects[i];
        if (rect.rect.extent.width == cb_state.base.render_area.extent.width &&
            rect.rect.extent.height == cb_state.base.render_area.extent.height) {
            return true;
        }
    }

    return false;
}

bool BestPractices::ValidateClearAttachment(const bp_state::CommandBufferSubState& cb_state, uint32_t fb_attachment,
                                            uint32_t color_attachment, VkImageAspectFlags aspects, const Location& loc) const {
    bool skip = false;
    const vvl::RenderPass* rp = cb_state.base.active_render_pass.get();

    if (!rp || fb_attachment == VK_ATTACHMENT_UNUSED) {
        return skip;
    }

    const auto& rp_state = cb_state.render_pass_state;

    auto attachment_itr =
        std::find_if(rp_state.touchesAttachments.begin(), rp_state.touchesAttachments.end(),
                     [fb_attachment](const bp_state::AttachmentInfo& info) { return info.framebufferAttachment == fb_attachment; });

    // Only report aspects which haven't been touched yet.
    VkImageAspectFlags new_aspects = aspects;
    if (attachment_itr != rp_state.touchesAttachments.end()) {
        new_aspects &= ~attachment_itr->aspects;
    }

    // Warn if this is issued prior to Draw Cmd and clearing the entire attachment
    if (!rp_state.has_draw_cmd) {
        const LogObjectList objlist(cb_state.Handle(), rp->Handle());
        skip |= LogPerformanceWarning("BestPractices-DrawState-ClearCmdBeforeDraw", objlist, loc,
                                      "issued on %s prior to any Draw Cmds in current render pass. It is recommended you "
                                      "use RenderPass LOAD_OP_CLEAR on attachments instead.",
                                      FormatHandle(cb_state.Handle()).c_str());
    }

    if ((new_aspects & VK_IMAGE_ASPECT_COLOR_BIT) &&
        rp->create_info.pAttachments[fb_attachment].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
        const LogObjectList objlist(cb_state.Handle(), rp->Handle());
        skip |=
            LogPerformanceWarning("BestPractices-vkCmdClearAttachments-clear-after-load-color", objlist, loc,
                                  "issued on %s for color attachment #%u in this subpass, "
                                  "but LOAD_OP_LOAD was used. If you need to clear the framebuffer, always use LOAD_OP_CLEAR as "
                                  "it is more efficient.",
                                  FormatHandle(cb_state.Handle()).c_str(), color_attachment);
    }

    if ((new_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
        rp->create_info.pAttachments[fb_attachment].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
        const LogObjectList objlist(cb_state.Handle(), rp->Handle());
        skip |=
            LogPerformanceWarning("BestPractices-vkCmdClearAttachments-clear-after-load-depth", objlist, loc,
                                  "issued on %s for the depth attachment in this subpass, "
                                  "but LOAD_OP_LOAD was used. If you need to clear the framebuffer, always use LOAD_OP_CLEAR as "
                                  "it is more efficient.",
                                  FormatHandle(cb_state.Handle()).c_str());

        if (VendorCheckEnabled(kBPVendorNVIDIA)) {
            skip |= ValidateZcullScope(cb_state, loc);
        }
    }

    if ((new_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
        rp->create_info.pAttachments[fb_attachment].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
        const LogObjectList objlist(cb_state.Handle(), rp->Handle());
        skip |=
            LogPerformanceWarning("BestPractices-vkCmdClearAttachments-clear-after-load-stencil", objlist, loc,
                                  "issued on %s for the stencil attachment in this subpass, "
                                  "but LOAD_OP_LOAD was used. If you need to clear the framebuffer, always use LOAD_OP_CLEAR as "
                                  "it is more efficient.",
                                  FormatHandle(cb_state.Handle()).c_str());
    }

    return skip;
}

bool BestPractices::PreCallValidateCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                                                       const VkClearAttachment* pAttachments, uint32_t rectCount,
                                                       const VkClearRect* pRects, const ErrorObject& error_obj) const {
    bool skip = false;
    const auto cb_state = GetRead<vvl::CommandBuffer>(commandBuffer);
    const auto& sub_state = bp_state::SubState(*cb_state);

    if (cb_state->IsSecondary()) {
        // Defer checks to ExecuteCommands.
        return skip;
    }

    // Only care about full clears, partial clears might have legitimate uses.
    const bool is_full_clear = ClearAttachmentsIsFullClear(sub_state, rectCount, pRects);

    // Check for uses of ClearAttachments along with LOAD_OP_LOAD,
    // as it can be more efficient to just use LOAD_OP_CLEAR
    const vvl::RenderPass* rp_state = cb_state->active_render_pass.get();
    if (rp_state) {
        if (rp_state->UsesDynamicRendering()) {
            const auto pColorAttachments = rp_state->dynamic_rendering_begin_rendering_info.pColorAttachments;

            if (VendorCheckEnabled(kBPVendorNVIDIA)) {
                for (uint32_t i = 0; i < attachmentCount; i++) {
                    const auto& attachment = pAttachments[i];
                    if (attachment.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
                        skip |= ValidateZcullScope(sub_state, error_obj.location);
                    }
                    if ((attachment.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) && attachment.colorAttachment != VK_ATTACHMENT_UNUSED) {
                        const auto& color_attachment = pColorAttachments[attachment.colorAttachment];
                        if (color_attachment.imageView) {
                            if (auto image_view_state = Get<vvl::ImageView>(color_attachment.imageView)) {
                                const VkFormat format = image_view_state->create_info.format;
                                skip |= ValidateClearColor(commandBuffer, format, attachment.clearValue.color, error_obj.location);
                            }
                        }
                    }
                }
            }

            if (is_full_clear) {
                // TODO: Implement ValidateClearAttachment for dynamic rendering
            }

        } else {
            const auto& subpass = rp_state->create_info.pSubpasses[cb_state->GetActiveSubpass()];

            if (is_full_clear) {
                for (uint32_t i = 0; i < attachmentCount; i++) {
                    const auto& attachment = pAttachments[i];

                    if (attachment.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
                        uint32_t color_attachment = attachment.colorAttachment;
                        uint32_t fb_attachment = subpass.pColorAttachments[color_attachment].attachment;
                        skip |= ValidateClearAttachment(sub_state, fb_attachment, color_attachment, attachment.aspectMask,
                                                        error_obj.location);
                    }

                    if (subpass.pDepthStencilAttachment &&
                        (attachment.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
                        uint32_t fb_attachment = subpass.pDepthStencilAttachment->attachment;
                        skip |= ValidateClearAttachment(sub_state, fb_attachment, VK_ATTACHMENT_UNUSED, attachment.aspectMask,
                                                        error_obj.location);
                    }
                }
            }
            if (VendorCheckEnabled(kBPVendorNVIDIA) && rp_state->create_info.pAttachments) {
                for (uint32_t attachment_idx = 0; attachment_idx < attachmentCount; ++attachment_idx) {
                    const auto& attachment = pAttachments[attachment_idx];

                    if (attachment.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
                        const uint32_t fb_attachment = subpass.pColorAttachments[attachment.colorAttachment].attachment;
                        if (fb_attachment != VK_ATTACHMENT_UNUSED) {
                            const VkFormat format = rp_state->create_info.pAttachments[fb_attachment].format;
                            skip |= ValidateClearColor(commandBuffer, format, attachment.clearValue.color, error_obj.location);
                        }
                    }
                }
            }
        }
    }

    if (VendorCheckEnabled(kBPVendorAMD)) {
        for (uint32_t attachment_idx = 0; attachment_idx < attachmentCount; attachment_idx++) {
            if (pAttachments[attachment_idx].aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
                bool black_check = false;
                black_check |= pAttachments[attachment_idx].clearValue.color.float32[0] != 0.0f;
                black_check |= pAttachments[attachment_idx].clearValue.color.float32[1] != 0.0f;
                black_check |= pAttachments[attachment_idx].clearValue.color.float32[2] != 0.0f;
                black_check |= pAttachments[attachment_idx].clearValue.color.float32[3] != 0.0f &&
                               pAttachments[attachment_idx].clearValue.color.float32[3] != 1.0f;

                bool white_check = false;
                white_check |= pAttachments[attachment_idx].clearValue.color.float32[0] != 1.0f;
                white_check |= pAttachments[attachment_idx].clearValue.color.float32[1] != 1.0f;
                white_check |= pAttachments[attachment_idx].clearValue.color.float32[2] != 1.0f;
                white_check |= pAttachments[attachment_idx].clearValue.color.float32[3] != 0.0f &&
                               pAttachments[attachment_idx].clearValue.color.float32[3] != 1.0f;

                if (black_check && white_check) {
                    skip |= LogPerformanceWarning("BestPractices-AMD-ClearAttachment-FastClearValues-color", commandBuffer,
                                                  error_obj.location,
                                                  "%s clear value for color attachment %" PRId32
                                                  " is not a fast clear value."
                                                  "Consider changing to one of the following:\n"
                                                  "RGBA(0, 0, 0, 0)\n "
                                                  "RGBA(0, 0, 0, 1)\n "
                                                  "RGBA(1, 1, 1, 0)\n "
                                                  "RGBA(1, 1, 1, 1)",
                                                  VendorSpecificTag(kBPVendorAMD), attachment_idx);
                }
            } else {
                if ((pAttachments[attachment_idx].clearValue.depthStencil.depth != 0 &&
                     pAttachments[attachment_idx].clearValue.depthStencil.depth != 1) &&
                    pAttachments[attachment_idx].clearValue.depthStencil.stencil != 0) {
                    skip |= LogPerformanceWarning("BestPractices-AMD-ClearAttachment-FastClearValues-depth-stencil", commandBuffer,
                                                  error_obj.location,
                                                  "%s clear value for depth/stencil "
                                                  "attachment %" PRId32
                                                  " is not a fast clear value."
                                                  "Consider changing to one of the following:\n"
                                                  "D=0.0f, S=0\n"
                                                  "D=1.0f, S=0",
                                                  VendorSpecificTag(kBPVendorAMD), attachment_idx);
                }
            }
        }
    }

    return skip;
}

bool BestPractices::ValidateCmdResolveImage(VkCommandBuffer command_buffer, VkImage src_image, VkImage dst_image,
                                            const Location& loc) const {
    bool skip = false;
    auto src_image_state = Get<vvl::Image>(src_image);
    auto dst_image_state = Get<vvl::Image>(dst_image);
    ASSERT_AND_RETURN_SKIP(src_image_state && dst_image_state);

    if (VendorCheckEnabled(kBPVendorArm)) {
        const LogObjectList objlist(command_buffer, src_image, dst_image);
        skip |=
            LogPerformanceWarning("BestPractices-Arm-vkCmdResolveImage-resolving-image", objlist, loc,
                                  "%s Attempting to resolve a multisampled image. "
                                  "This is a very slow and extremely bandwidth intensive path. "
                                  "You should always resolve multisampled images on-tile with pResolveAttachments in VkRenderPass.",
                                  VendorSpecificTag(kBPVendorArm));
    }
    return skip;
}

bool BestPractices::PreCallValidateCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                                   VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                                   const VkImageResolve* pRegions, const ErrorObject& error_obj) const {
    bool skip = false;
    skip |= ValidateCmdResolveImage(commandBuffer, srcImage, dstImage, error_obj.location);
    return skip;
}

bool BestPractices::PreCallValidateCmdResolveImage2KHR(VkCommandBuffer commandBuffer,
                                                       const VkResolveImageInfo2KHR* pResolveImageInfo,
                                                       const ErrorObject& error_obj) const {
    return PreCallValidateCmdResolveImage2(commandBuffer, pResolveImageInfo, error_obj);
}

bool BestPractices::PreCallValidateCmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2* pResolveImageInfo,
                                                    const ErrorObject& error_obj) const {
    bool skip = false;
    skip |= ValidateCmdResolveImage(commandBuffer, pResolveImageInfo->srcImage, pResolveImageInfo->dstImage,
                                    error_obj.location.dot(Field::pResolveImageInfo));
    return skip;
}

template <typename RegionType>
bool BestPractices::ValidateCmdBlitImage(VkCommandBuffer command_buffer, uint32_t region_count, const RegionType* regions,
                                         const Location& loc) const {
    bool skip = false;
    for (uint32_t i = 0; i < region_count; i++) {
        const RegionType region = regions[i];
        if ((region.srcOffsets[0].x == region.srcOffsets[1].x) || (region.srcOffsets[0].y == region.srcOffsets[1].y) ||
            (region.srcOffsets[0].z == region.srcOffsets[1].z)) {
            skip |= LogWarning("BestPractices-DrawState-InvalidExtents-src", command_buffer,
                               loc.dot(Field::pRegions, i).dot(Field::srcOffsets), "specify a zero-volume area");
        }
        if ((region.dstOffsets[0].x == region.dstOffsets[1].x) || (region.dstOffsets[0].y == region.dstOffsets[1].y) ||
            (region.dstOffsets[0].z == region.dstOffsets[1].z)) {
            skip |= LogWarning("BestPractices-DrawState-InvalidExtents-dst", command_buffer,
                               loc.dot(Field::pRegions, i).dot(Field::dstOffsets), "specify a zero-volume area");
        }
    }
    return skip;
}

bool BestPractices::PreCallValidateCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                                VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                                const VkImageBlit* pRegions, VkFilter filter, const ErrorObject& error_obj) const {
    return ValidateCmdBlitImage(commandBuffer, regionCount, pRegions, error_obj.location);
}

bool BestPractices::PreCallValidateCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const VkBlitImageInfo2KHR* pBlitImageInfo,
                                                    const ErrorObject& error_obj) const {
    return PreCallValidateCmdBlitImage2(commandBuffer, pBlitImageInfo, error_obj);
}

bool BestPractices::PreCallValidateCmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2* pBlitImageInfo,
                                                 const ErrorObject& error_obj) const {
    return ValidateCmdBlitImage(commandBuffer, pBlitImageInfo->regionCount, pBlitImageInfo->pRegions,
                                error_obj.location.dot(Field::pBlitImageInfo));
}

bool BestPractices::PreCallValidateCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                                      const VkClearColorValue* pColor, uint32_t rangeCount,
                                                      const VkImageSubresourceRange* pRanges, const ErrorObject& error_obj) const {
    bool skip = false;

    auto dst_image = Get<vvl::Image>(image);
    ASSERT_AND_RETURN_SKIP(dst_image);

    if (VendorCheckEnabled(kBPVendorAMD)) {
        skip |= LogPerformanceWarning("BestPractices-AMD-ClearAttachment-ClearImage-color", commandBuffer, error_obj.location,
                                      "%s using vkCmdClearColorImage is not recommended. Prefer using LOAD_OP_CLEAR or "
                                      "vkCmdClearAttachments instead",
                                      VendorSpecificTag(kBPVendorAMD));
    }
    if (VendorCheckEnabled(kBPVendorNVIDIA)) {
        skip |= ValidateClearColor(commandBuffer, dst_image->create_info.format, *pColor, error_obj.location);
    }

    return skip;
}

bool BestPractices::PreCallValidateCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
                                                             VkImageLayout imageLayout,
                                                             const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount,
                                                             const VkImageSubresourceRange* pRanges,
                                                             const ErrorObject& error_obj) const {
    bool skip = false;
    if (VendorCheckEnabled(kBPVendorAMD)) {
        skip |=
            LogPerformanceWarning("BestPractices-AMD-ClearAttachment-ClearImage-depth-stencil", commandBuffer, error_obj.location,
                                  "%s using vkCmdClearDepthStencilImage is not recommended. Prefer using LOAD_OP_CLEAR or "
                                  "vkCmdClearAttachments instead",
                                  VendorSpecificTag(kBPVendorAMD));
    }
    const auto cb_state = GetRead<vvl::CommandBuffer>(commandBuffer);
    if (VendorCheckEnabled(kBPVendorNVIDIA)) {
        const auto& sub_state = bp_state::SubState(*cb_state);
        for (uint32_t i = 0; i < rangeCount; i++) {
            skip |= ValidateZcull(sub_state, image, pRanges[i], error_obj.location);
        }
    }

    return skip;
}

bool BestPractices::PreCallValidateCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                                                VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                                                const VkImageCopy* pRegions, const ErrorObject& error_obj) const {
    bool skip = false;

    if (VendorCheckEnabled(kBPVendorAMD)) {
        auto src_state = Get<vvl::Image>(srcImage);
        auto dst_state = Get<vvl::Image>(dstImage);

        if (src_state && dst_state) {
            VkImageTiling src_tiling = src_state->create_info.tiling;
            VkImageTiling dst_tiling = dst_state->create_info.tiling;
            if (src_tiling != dst_tiling && (src_tiling == VK_IMAGE_TILING_LINEAR || dst_tiling == VK_IMAGE_TILING_LINEAR)) {
                const LogObjectList objlist(commandBuffer, srcImage, dstImage);
                skip |= LogPerformanceWarning("BestPractices-AMD-vkImage-AvoidImageToImageCopy", objlist, error_obj.location,
                                              "%s srcImage (%s) and dstImage (%s) have differing tilings. Use buffer to "
                                              "image (vkCmdCopyImageToBuffer) "
                                              "and image to buffer (vkCmdCopyBufferToImage) copies instead of image to image "
                                              "copies when converting between linear and optimal images",
                                              VendorSpecificTag(kBPVendorAMD), FormatHandle(srcImage).c_str(),
                                              FormatHandle(dstImage).c_str());
            }
        }
    }

    return skip;
}
