/*
 * Copyright (c) 2015-2025 The Khronos Group Inc.
 * Copyright (c) 2015-2025 Valve Corporation
 * Copyright (c) 2015-2025 LunarG, Inc.
 * Copyright (c) 2015-2025 Google, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include <spirv-tools/libspirv.h>
#include <vulkan/vulkan_core.h>
#include "../framework/layer_validation_tests.h"
#include "../framework/pipeline_helper.h"
#include "../framework/descriptor_helper.h"

class PositiveYcbcr : public VkLayerTest {};

TEST_F(PositiveYcbcr, PlaneAspectNone) {
    SetTargetApiVersion(VK_API_VERSION_1_3);
    RETURN_IF_SKIP(Init());

    VkImageCreateInfo image_createinfo = vku::InitStructHelper();
    image_createinfo.imageType = VK_IMAGE_TYPE_2D;
    image_createinfo.format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    image_createinfo.extent = {128, 128, 1};
    image_createinfo.mipLevels = 1;
    image_createinfo.arrayLayers = 1;
    image_createinfo.samples = VK_SAMPLE_COUNT_1_BIT;
    image_createinfo.tiling = VK_IMAGE_TILING_LINEAR;
    image_createinfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkDeviceImageMemoryRequirements image_mem_reqs = vku::InitStructHelper();
    image_mem_reqs.pCreateInfo = &image_createinfo;
    image_mem_reqs.planeAspect = VK_IMAGE_ASPECT_NONE;
    VkMemoryRequirements2 mem_reqs_2 = vku::InitStructHelper();
    vk::GetDeviceImageMemoryRequirements(device(), &image_mem_reqs, &mem_reqs_2);
}

TEST_F(PositiveYcbcr, MultiplaneGetImageSubresourceLayout) {
    TEST_DESCRIPTION("Positive test, query layout of a single plane of a multiplane image. (repro Github #2530)");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    auto ci = vku::InitStruct<VkImageCreateInfo>();
    ci.flags = 0;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    ci.extent = {128, 128, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_LINEAR;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    // Verify format
    if (!ImageFormatIsSupported(instance(), Gpu(), ci, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
        // Assume there's low ROI on searching for different mp formats
        GTEST_SKIP() << "Multiplane image format not supported";
    }
    vkt::Image image(*m_device, ci, vkt::no_mem);

    // Query layout of 3rd plane
    VkImageSubresource subres = {};
    subres.aspectMask = VK_IMAGE_ASPECT_PLANE_2_BIT;
    subres.mipLevel = 0;
    subres.arrayLayer = 0;
    VkSubresourceLayout layout = {};

    vk::GetImageSubresourceLayout(device(), image, &subres, &layout);
}

TEST_F(PositiveYcbcr, MultiplaneImageCopyBufferToImage) {
    TEST_DESCRIPTION("Positive test of multiplane copy buffer to image");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    auto ci = vku::InitStruct<VkImageCreateInfo>();
    ci.flags = 0;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;  // All planes of equal extent
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.extent = {16, 16, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;

    VkFormatFeatureFlags features = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), ci, features)) {
        // Assume there's low ROI on searching for different mp formats
        GTEST_SKIP() << "Multiplane image format not supported";
    }
    vkt::Image image(*m_device, ci);

    m_command_buffer.Reset();
    m_command_buffer.Begin();
    image.ImageMemoryBarrier(m_command_buffer, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    std::array<VkImageAspectFlagBits, 3> aspects = {
        {VK_IMAGE_ASPECT_PLANE_0_BIT, VK_IMAGE_ASPECT_PLANE_1_BIT, VK_IMAGE_ASPECT_PLANE_2_BIT}};
    std::array<vkt::Buffer, 3> buffers;

    VkBufferImageCopy copy = {};
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {16, 16, 1};

    for (size_t i = 0; i < aspects.size(); ++i) {
        buffers[i].Init(*m_device, 16 * 16 * 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        copy.imageSubresource.aspectMask = aspects[i];
        vk::CmdCopyBufferToImage(m_command_buffer, buffers[i], image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }
    m_command_buffer.End();
}

TEST_F(PositiveYcbcr, MultiplaneImageCopy) {
    TEST_DESCRIPTION("Copy Plane 0 to Plane 2");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    if (!FormatFeaturesAreSupported(Gpu(), VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                    VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT)) {
        GTEST_SKIP() << "Required formats/features not supported";
    }

    auto ci = vku::InitStruct<VkImageCreateInfo>();
    ci.flags = 0;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;  // All planes of equal extent
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.extent = {128, 128, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;

    // Verify format
    VkFormatFeatureFlags features = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), ci, features)) {
        // Assume there's low ROI on searching for different mp formats
        GTEST_SKIP() << "Multiplane image format not supported";
    }

    vkt::Image image(*m_device, ci, vkt::no_mem);
    vkt::DeviceMemory mem_obj(*m_device, vkt::DeviceMemory::GetResourceAllocInfo(*m_device, image.MemoryRequirements(), 0));

    vk::BindImageMemory(device(), image, mem_obj, 0);

    // Copy plane 0 to plane 2
    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1};
    copyRegion.srcOffset = {0, 0, 0};
    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_PLANE_2_BIT, 0, 0, 1};
    copyRegion.dstOffset = {0, 0, 0};
    copyRegion.extent = {128, 128, 1};

    m_command_buffer.Begin();
    image.ImageMemoryBarrier(m_command_buffer, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vk::CmdCopyImage(m_command_buffer, image, VK_IMAGE_LAYOUT_GENERAL, image, VK_IMAGE_LAYOUT_GENERAL, 1, &copyRegion);
    m_command_buffer.End();

    m_default_queue->SubmitAndWait(m_command_buffer);
}

TEST_F(PositiveYcbcr, MultiplaneImageBindDisjoint) {
    TEST_DESCRIPTION("Bind image with disjoint memory");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    if (!FormatFeaturesAreSupported(Gpu(), VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                    VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT)) {
        GTEST_SKIP() << "Required formats/features not supported";
    }

    auto ci = vku::InitStruct<VkImageCreateInfo>();
    ci.flags = VK_IMAGE_CREATE_DISJOINT_BIT;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;  // All planes of equal extent
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.extent = {128, 128, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;

    // Verify format
    VkFormatFeatureFlags features =
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_DISJOINT_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), ci, features)) {
        // Assume there's low ROI on searching for different mp formats
        GTEST_SKIP() << "Multiplane image format not supported";
    }

    vkt::Image image(*m_device, ci, vkt::no_mem);

    // Allocate & bind memory
    auto image_plane_req = vku::InitStruct<VkImagePlaneMemoryRequirementsInfo>();
    auto mem_req_info2 = vku::InitStruct<VkImageMemoryRequirementsInfo2>(&image_plane_req);
    mem_req_info2.image = image;
    auto mem_reqs2 = vku::InitStruct<VkMemoryRequirements2>();

    VkMemoryPropertyFlags mem_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto alloc_info = vku::InitStruct<VkMemoryAllocateInfo>();

    // Plane 0
    image_plane_req.planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT;
    vk::GetImageMemoryRequirements2(device(), &mem_req_info2, &mem_reqs2);
    uint32_t mem_type = 0;
    auto phys_mem_props2 = vku::InitStruct<VkPhysicalDeviceMemoryProperties2>();
    vk::GetPhysicalDeviceMemoryProperties2(Gpu(), &phys_mem_props2);
    for (mem_type = 0; mem_type < phys_mem_props2.memoryProperties.memoryTypeCount; mem_type++) {
        if ((mem_reqs2.memoryRequirements.memoryTypeBits & (1 << mem_type)) &&
            ((phys_mem_props2.memoryProperties.memoryTypes[mem_type].propertyFlags & mem_props) == mem_props)) {
            alloc_info.memoryTypeIndex = mem_type;
            break;
        }
    }
    alloc_info.allocationSize = mem_reqs2.memoryRequirements.size;
    vkt::DeviceMemory p0_mem(*m_device, alloc_info);

    // Plane 1 & 2 use same memory type
    image_plane_req.planeAspect = VK_IMAGE_ASPECT_PLANE_1_BIT;
    vk::GetImageMemoryRequirements2(device(), &mem_req_info2, &mem_reqs2);
    alloc_info.allocationSize = mem_reqs2.memoryRequirements.size;
    vkt::DeviceMemory p1_mem(*m_device, alloc_info);

    image_plane_req.planeAspect = VK_IMAGE_ASPECT_PLANE_2_BIT;
    vk::GetImageMemoryRequirements2(device(), &mem_req_info2, &mem_reqs2);
    alloc_info.allocationSize = mem_reqs2.memoryRequirements.size;
    vkt::DeviceMemory p2_mem(*m_device, alloc_info);

    // Set up 3-plane binding
    VkBindImageMemoryInfo bind_info[3];
    VkBindImagePlaneMemoryInfo plane_info[3];
    for (int plane = 0; plane < 3; plane++) {
        plane_info[plane] = vku::InitStruct<VkBindImagePlaneMemoryInfo>();
        bind_info[plane] = vku::InitStruct<VkBindImageMemoryInfo>(&plane_info[plane]);
        bind_info[plane].image = image;
        bind_info[plane].memoryOffset = 0;
    }
    bind_info[0].memory = p0_mem;
    bind_info[1].memory = p1_mem;
    bind_info[2].memory = p2_mem;
    plane_info[0].planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT;
    plane_info[1].planeAspect = VK_IMAGE_ASPECT_PLANE_1_BIT;
    plane_info[2].planeAspect = VK_IMAGE_ASPECT_PLANE_2_BIT;

    vk::BindImageMemory2(device(), 3, bind_info);
}

TEST_F(PositiveYcbcr, ImageLayout) {
    TEST_DESCRIPTION("Test that changing the layout of ASPECT_COLOR also changes the layout of the individual planes");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();

    if (!FormatFeaturesAreSupported(Gpu(), VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                    VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT)) {
        GTEST_SKIP() << "Required formats/features not supported";
    }

    auto ci = vku::InitStruct<VkImageCreateInfo>();
    ci.flags = 0;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;  // All planes of equal extent
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.extent = {256, 256, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;

    // Verify format
    VkFormatFeatureFlags features = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), ci, features)) {
        // Assume there's low ROI on searching for different mp formats
        GTEST_SKIP() << "Multiplane image format not supported";
    }
    vkt::Image image(*m_device, ci, vkt::set_layout);
    vkt::Buffer buffer(*m_device, 128 * 128 * 3, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    VkBufferImageCopy copy_region = {};
    copy_region.bufferRowLength = 128;
    copy_region.bufferImageHeight = 128;
    copy_region.imageSubresource = {VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1};
    copy_region.imageExtent = {64, 64, 1};

    vk::ResetCommandBuffer(m_command_buffer, 0);
    m_command_buffer.Begin();
    image.ImageMemoryBarrier(m_command_buffer, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vk::CmdCopyBufferToImage(m_command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
    m_command_buffer.End();
    m_default_queue->SubmitAndWait(m_command_buffer);

    // Test to verify that views of multiplanar images have layouts tracked correctly
    // by changing the image's layout then using a view of that image
    vkt::SamplerYcbcrConversion conversion(*m_device, VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));

    auto ivci = vku::InitStruct<VkImageViewCreateInfo>(&conversion_info);
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM;
    ivci.subresourceRange.layerCount = 1;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkt::ImageView view(*m_device, ivci);

    OneOffDescriptorSet descriptor_set(
        m_device, {
                      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler.handle()},
                  });

    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorImageInfo(0, view, sampler);
    descriptor_set.UpdateDescriptorSets();

    CreatePipelineHelper pipe(*this);
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    VkImageMemoryBarrier img_barrier = vku::InitStructHelper();
    img_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_barrier.image = image;
    img_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    img_barrier.subresourceRange.baseArrayLayer = 0;
    img_barrier.subresourceRange.baseMipLevel = 0;
    img_barrier.subresourceRange.layerCount = 1;
    img_barrier.subresourceRange.levelCount = 1;
    vk::CmdPipelineBarrier(m_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                           VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &img_barrier);
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);

    vk::CmdDraw(m_command_buffer, 1, 0, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();
    m_default_queue->SubmitAndWait(m_command_buffer);
}

TEST_F(PositiveYcbcr, DrawCombinedImageSampler) {
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();
    const VkFormat format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;

    auto ci = vku::InitStruct<VkImageCreateInfo>();
    ci.flags = 0;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.extent = {256, 256, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), ci, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        // Assume there's low ROI on searching for different mp formats
        GTEST_SKIP() << "Multiplane image format not supported";
    }
    vkt::Image image(*m_device, ci, vkt::set_layout);

    vkt::SamplerYcbcrConversion conversion(*m_device, format);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));
    vkt::ImageView view = image.CreateView(VK_IMAGE_ASPECT_COLOR_BIT, &conversion_info);

    OneOffDescriptorSet descriptor_set(
        m_device, {
                      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler.handle()},
                  });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorImageInfo(0, view, sampler);
    descriptor_set.UpdateDescriptorSets();

    const char fsSource[] = R"glsl(
        #version 450
        layout (set = 0, binding = 0) uniform sampler2D ycbcr;
        layout(location=0) out vec4 out_color;
        void main() {
            out_color = texture(ycbcr, vec2(0));
        }
    )glsl";
    VkShaderObj fs(this, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {pipe.vs_->GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 1, 0, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();
}

TEST_F(PositiveYcbcr, ImageQuerySizeLod) {
    TEST_DESCRIPTION("https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/7903");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());
    InitRenderTarget();
    const VkFormat format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;

    auto ci = vku::InitStruct<VkImageCreateInfo>();
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.extent = {256, 256, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), ci, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        // Assume there's low ROI on searching for different mp formats
        GTEST_SKIP() << "Multiplane image format not supported";
    }
    vkt::Image image(*m_device, ci, vkt::set_layout);

    vkt::SamplerYcbcrConversion conversion(*m_device, format);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));
    vkt::ImageView view = image.CreateView(VK_IMAGE_ASPECT_COLOR_BIT, &conversion_info);

    OneOffDescriptorSet descriptor_set(
        m_device, {
                      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler.handle()},
                  });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});
    descriptor_set.WriteDescriptorImageInfo(0, view, sampler);
    descriptor_set.UpdateDescriptorSets();

    const char fsSource[] = R"glsl(
        #version 450
        layout (set = 0, binding = 0) uniform sampler2D ycbcr;
        layout(location=0) out vec4 out_color;
        void main() {
            int x = textureSize(ycbcr, 0).x;
            out_color = vec4(0.0) * x;
        }
    )glsl";
    VkShaderObj fs(this, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    CreatePipelineHelper pipe(*this);
    pipe.shader_stages_ = {pipe.vs_->GetStageCreateInfo(), fs.GetStageCreateInfo()};
    pipe.gp_ci_.layout = pipeline_layout;
    pipe.CreateGraphicsPipeline();

    m_command_buffer.Begin();
    m_command_buffer.BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vk::CmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set.set_, 0,
                              nullptr);
    vk::CmdDraw(m_command_buffer, 1, 0, 0, 0);
    m_command_buffer.EndRenderPass();
    m_command_buffer.End();
}

TEST_F(PositiveYcbcr, FormatCompatibilitySamePlane) {
    AddRequiredExtensions(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    AddRequiredExtensions(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    RETURN_IF_SKIP(Init());

    const VkFormat formats[2] = {VK_FORMAT_R8_SNORM, VK_FORMAT_R8_UNORM};

    VkImageFormatListCreateInfo format_list = vku::InitStructHelper();
    format_list.viewFormatCount = 2;
    format_list.pViewFormats = formats;

    VkImageCreateInfo image_create_info = vku::InitStructHelper(&format_list);
    // all three planes are VK_FORMAT_R8_UNORM
    image_create_info.format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    image_create_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent = {32, 32, 1};
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), image_create_info, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        GTEST_SKIP() << "Multiplane image format not supported";
    }
    vkt::Image image(*m_device, image_create_info);
}

TEST_F(PositiveYcbcr, FormatCompatibilityDifferentPlane) {
    AddRequiredExtensions(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    AddRequiredExtensions(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    RETURN_IF_SKIP(Init());

    const VkFormat formats[3] = {VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8_SNORM};

    VkImageFormatListCreateInfo format_list = vku::InitStructHelper();
    format_list.viewFormatCount = 3;
    format_list.pViewFormats = formats;

    VkImageCreateInfo image_create_info = vku::InitStructHelper(&format_list);
    // planes are VK_FORMAT_R8_UNORM and VK_FORMAT_R8G8_UNORM
    image_create_info.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    image_create_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.extent = {32, 32, 1};
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!ImageFormatIsSupported(instance(), Gpu(), image_create_info, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        GTEST_SKIP() << "Multiplane image format not supported";
    }
    vkt::Image image(*m_device, image_create_info);
}

// Being decided in https://gitlab.khronos.org/vulkan/vulkan/-/issues/4151
TEST_F(PositiveYcbcr, DISABLED_CopyImageSinglePlane422Alignment) {
    AddRequiredExtensions(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    VkImageCreateInfo image_ci = vkt::Image::ImageCreateInfo2D(64, 64, 1, 1, VK_FORMAT_G8B8G8R8_422_UNORM,
                                                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    VkFormatFeatureFlags features = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    bool supported = ImageFormatIsSupported(instance(), Gpu(), image_ci, features);
    if (!supported) {
        GTEST_SKIP() << "Single-plane _422 image format not supported";
    }

    vkt::Image image_422(*m_device, image_ci, vkt::set_layout);

    image_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_ci.extent = {32, 64, 1};
    vkt::Image image_color(*m_device, image_ci, vkt::set_layout);

    m_command_buffer.Begin();

    VkImageCopy copy_region;
    copy_region.extent = {1, 1, 1};
    copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.srcSubresource.mipLevel = 0;
    copy_region.srcSubresource.baseArrayLayer = 0;
    copy_region.srcSubresource.layerCount = 1;
    copy_region.dstSubresource = copy_region.srcSubresource;
    copy_region.srcOffset = {0, 0, 0};
    copy_region.dstOffset = {0, 0, 0};

    vk::CmdCopyImage(m_command_buffer, image_color, VK_IMAGE_LAYOUT_GENERAL, image_422, VK_IMAGE_LAYOUT_GENERAL, 1, &copy_region);

    m_command_buffer.End();
}

TEST_F(PositiveYcbcr, NonCombinedSampledImage) {
    TEST_DESCRIPTION("Basic use of YCbCr with no arrays");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    vkt::SamplerYcbcrConversion conversion(*m_device, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));

    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                           {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sampler.handle()},
                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    const char* cs_source = R"glsl(
        #version 460
        layout(set = 0, binding = 0) uniform texture2D kTextures2D;
        layout(set = 0, binding = 1) uniform sampler kSamplers;
        layout(set = 0, binding = 2) buffer SSBO {
            vec4 result;
        };

        void main() {
            result = texture(sampler2D(kTextures2D, kSamplers), vec2(0));
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}

TEST_F(PositiveYcbcr, DescriptorIndexCombinedSampledImage) {
    TEST_DESCRIPTION("Basic use of YCbCr with arrays, but static constant");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    vkt::SamplerYcbcrConversion conversion(*m_device, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));

    VkSampler samplers[4] = {sampler, sampler, sampler, sampler};
    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, samplers},
                                           {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    const char* cs_source = R"glsl(
        #version 460
        layout(set = 0, binding = 0) uniform sampler2D ycbcr[4];
        layout(set = 0, binding = 1) buffer SSBO {
            vec4 result;
        };

        void main() {
            result = texture(ycbcr[2], vec2(0));
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}

TEST_F(PositiveYcbcr, DescriptorIndexCombinedSampledImageRuntimeArray) {
    TEST_DESCRIPTION("Basic use of YCbCr with arrays, but static constant in a runtime array");
    AddRequiredExtensions(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    SetTargetApiVersion(VK_API_VERSION_1_2);
    AddRequiredFeature(vkt::Feature::runtimeDescriptorArray);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    vkt::SamplerYcbcrConversion conversion(*m_device, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));

    VkSampler samplers[4] = {sampler, sampler, sampler, sampler};
    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, samplers},
                                           {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    // GLSL, but hardcode it to use a runtime descriptor array
    // layout(set = 0, binding = 0) uniform sampler2D ycbcr[];
    // layout(set = 0, binding = 1) buffer SSBO { vec4 result; };
    // void main() {
    //     result = texture(ycbcr[2], vec2(0));
    // }
    const char* cs_source = R"(
               OpCapability Shader
               OpCapability RuntimeDescriptorArray
          %2 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %main "main" %_ %ycbcr
               OpExecutionMode %main LocalSize 1 1 1
               OpDecorate %SSBO Block
               OpMemberDecorate %SSBO 0 Offset 0
               OpDecorate %_ Binding 1
               OpDecorate %_ DescriptorSet 0
               OpDecorate %ycbcr Binding 0
               OpDecorate %ycbcr DescriptorSet 0
       %void = OpTypeVoid
          %4 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
       %SSBO = OpTypeStruct %v4float
%_ptr_StorageBuffer_SSBO = OpTypePointer StorageBuffer %SSBO
          %_ = OpVariable %_ptr_StorageBuffer_SSBO StorageBuffer
        %int = OpTypeInt 32 1
      %int_0 = OpConstant %int 0
         %15 = OpTypeImage %float 2D 0 0 0 1 Unknown
         %16 = OpTypeSampledImage %15
%_runtimearr_16 = OpTypeRuntimeArray %16
%_ptr_UniformConstant__runtimearr_16 = OpTypePointer UniformConstant %_runtimearr_16
      %ycbcr = OpVariable %_ptr_UniformConstant__runtimearr_16 UniformConstant
      %int_1 = OpConstant %int 1
%_ptr_UniformConstant_16 = OpTypePointer UniformConstant %16
    %v2float = OpTypeVector %float 2
    %float_0 = OpConstant %float 0
         %29 = OpConstantComposite %v2float %float_0 %float_0
%_ptr_StorageBuffer_v4float = OpTypePointer StorageBuffer %v4float
      %int_2 = OpConstant %int 2
       %main = OpFunction %void None %4
          %6 = OpLabel
         %34 = OpAccessChain %_ptr_UniformConstant_16 %ycbcr %int_2
         %35 = OpLoad %16 %34
         %36 = OpImageSampleExplicitLod %v4float %35 %29 Lod %float_0
         %37 = OpAccessChain %_ptr_StorageBuffer_v4float %_ %int_0
               OpStore %37 %36
               OpReturn
               OpFunctionEnd
    )";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT, SPV_ENV_VULKAN_1_2, SPV_SOURCE_ASM);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}

TEST_F(PositiveYcbcr, DescriptorIndexNonCombinedSampledImage) {
    TEST_DESCRIPTION("Basic use of YCbCr with arrays, but static constant");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    vkt::SamplerYcbcrConversion conversion(*m_device, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));

    VkSampler samplers[4] = {sampler, sampler, sampler, sampler};
    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                           {1, VK_DESCRIPTOR_TYPE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, samplers},
                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    const char* cs_source = R"glsl(
        #version 460
        layout(set = 0, binding = 0) uniform texture2D kTextures2D[4];
        layout(set = 0, binding = 1) uniform sampler kSamplers[4];
        layout(set = 0, binding = 2) buffer SSBO {
            vec4 result;
        };

        void main() {
            result = texture(sampler2D(kTextures2D[1], kSamplers[0]), vec2(0));
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}

TEST_F(PositiveYcbcr, DescriptorIndexNonCombinedSampledImageMix) {
    TEST_DESCRIPTION("Some samplers are dynamic and some static, but all static are YCbCr");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    vkt::SamplerYcbcrConversion conversion(*m_device, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));

    VkSampler samplers[4] = {sampler, sampler, sampler, sampler};
    OneOffDescriptorSet descriptor_set0(m_device,
                                        {
                                            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, samplers},
                                            {2, VK_DESCRIPTOR_TYPE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                        });
    OneOffDescriptorSet descriptor_set1(m_device, {
                                                      {0, VK_DESCRIPTOR_TYPE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                  });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set0.layout_, &descriptor_set1.layout_});

    const char* cs_source = R"glsl(
        #version 460
        layout(set = 0, binding = 0) uniform texture2D kTextures2D[4];
        layout(set = 0, binding = 1) uniform sampler normal0[4];
        layout(set = 1, binding = 0) uniform sampler normal1[4];
        layout(set = 0, binding = 2) uniform sampler ycbcr[4];
        layout(set = 0, binding = 3) buffer SSBO {
            vec4 result;
            uint x;
        };

        void main() {
            result = texture(sampler2D(kTextures2D[x], normal0[1]), vec2(0));
            result += texture(sampler2D(kTextures2D[1], normal0[x]), vec2(0));
            result = texture(sampler2D(kTextures2D[1], normal0[1]), vec2(0));
            result += texture(sampler2D(kTextures2D[1], ycbcr[0]), vec2(0));
            result += texture(sampler2D(kTextures2D[1], normal1[x]), vec2(0));
            result += texture(sampler2D(kTextures2D[x], normal1[1]), vec2(0));
            result += texture(sampler2D(kTextures2D[1], normal1[1]), vec2(0));
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}

TEST_F(PositiveYcbcr, EmbeddedCombinedSampledImage) {
    TEST_DESCRIPTION("Make sure non-YCbCr embedded samplers are not checked");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    RETURN_IF_SKIP(Init());

    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo());
    VkSampler samplers[4] = {sampler, sampler, sampler, sampler};
    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, samplers},
                                           {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    const char* cs_source = R"glsl(
        #version 460
        layout(set = 0, binding = 0) uniform sampler2D kCombined[4];
        layout(set = 0, binding = 1) buffer SSBO {
            vec4 result;
            uint x;
        };

        void main() {
            result = texture(kCombined[x], vec2(0));
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}

TEST_F(PositiveYcbcr, EmbeddedNonCombinedSampledImage) {
    TEST_DESCRIPTION("Make sure non-YCbCr embedded samplers are not checked");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    RETURN_IF_SKIP(Init());

    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo());
    VkSampler samplers[4] = {sampler, sampler, sampler, sampler};
    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                           {1, VK_DESCRIPTOR_TYPE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, samplers},
                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    const char* cs_source = R"glsl(
        #version 460
        layout(set = 0, binding = 0) uniform texture2D kTextures2D[4];
        layout(set = 0, binding = 1) uniform sampler kSamplers[4];
        layout(set = 0, binding = 2) buffer SSBO {
            vec4 result;
            uint x;
        };

        void main() {
            result = texture(sampler2D(kTextures2D[x], kSamplers[x]), vec2(0));
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}

TEST_F(PositiveYcbcr, DescriptorIndexSpecConstant) {
    TEST_DESCRIPTION("Make sure spec constant count as static");
    SetTargetApiVersion(VK_API_VERSION_1_1);
    AddRequiredFeature(vkt::Feature::samplerYcbcrConversion);
    RETURN_IF_SKIP(Init());

    vkt::SamplerYcbcrConversion conversion(*m_device, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
    auto conversion_info = conversion.ConversionInfo();
    vkt::Sampler sampler(*m_device, SafeSaneSamplerCreateInfo(&conversion_info));

    VkSampler samplers[4] = {sampler, sampler, sampler, sampler};
    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                           {1, VK_DESCRIPTOR_TYPE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, samplers},
                                           {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                           {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                       });
    const vkt::PipelineLayout pipeline_layout(*m_device, {&descriptor_set.layout_});

    const char* cs_source = R"glsl(
        #version 460
        layout (constant_id = 0) const int x = 2;
        layout (constant_id = 1) const int y = 1;
        layout (constant_id = 2) const int z = 3;
        layout(set = 0, binding = 0) uniform texture2D kTextures2D[4];
        layout(set = 0, binding = 1) uniform sampler kSamplers[4];
        layout(set = 0, binding = 2) uniform sampler2D kCombined[4];
        layout(set = 0, binding = 3) buffer SSBO {
            vec4 result;
        };

        void main() {
            result = texture(sampler2D(kTextures2D[x], kSamplers[y]), vec2(0));
            result += texture(kCombined[z], vec2(0));
        }
    )glsl";

    CreateComputePipelineHelper pipe(*this);
    pipe.cs_ = VkShaderObj(this, cs_source, VK_SHADER_STAGE_COMPUTE_BIT);
    pipe.cp_ci_.layout = pipeline_layout;
    pipe.CreateComputePipeline();
}
