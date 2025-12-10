/*
* File: renderer_upload.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include <iostream>

#include "renderer.hpp"

namespace blok {

static vk::ImageAspectFlags aspectFromFormat(vk::Format fmt) {
    switch (fmt) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
        return vk::ImageAspectFlagBits::eDepth;
    case vk::Format::eS8Uint:
        return vk::ImageAspectFlagBits::eStencil;
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
        return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    default:
        return vk::ImageAspectFlagBits::eColor;
    }
}

Buffer Renderer::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, VmaAllocationCreateFlags allocFlags, VmaMemoryUsage memUsage, bool mapped) {
    Buffer out{};
    out.size = size;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = static_cast<VkDeviceSize>(size);
    bci.usage = static_cast<VkBufferUsageFlags>(usage);
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.flags = allocFlags | (mapped ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0);
    aci.usage = memUsage;

    VmaAllocationInfo ainfo{};
    if (vmaCreateBuffer(m_allocator, &bci, &aci, reinterpret_cast<VkBuffer*>(&out.handle),
                        &out.alloc, &ainfo) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API: vmaCreateBuffer failed");
                        }
    if (mapped) out.mapped = ainfo.pMappedData;
    return out;
}

void Renderer::uploadToBuffer(const void *src, vk::DeviceSize size, Buffer &dst, vk::DeviceSize dstOffset) {
    if (dst.mapped) {
        std::memcpy(static_cast<char*>(dst.mapped) + dstOffset, src, static_cast<size_t>(size));
        return;
    }
    // Staging path
    Buffer staging = createBuffer(size,
                                  vk::BufferUsageFlagBits::eTransferSrc,
                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                  VMA_MEMORY_USAGE_AUTO_PREFER_HOST, true);
    std::memcpy(staging.mapped, src, static_cast<size_t>(size));
    copyBuffer(staging, dst, size);
    vmaDestroyBuffer(m_allocator, staging.handle, staging.alloc);
}

void Renderer::copyBuffer(Buffer &src, Buffer &dst, vk::DeviceSize size) {
    auto result = m_device.resetFences(1, &m_uploadFence);
    m_uploadCmd.reset({});

    vk::CommandBufferBeginInfo bi{};
    bi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    m_uploadCmd.begin(bi);

    vk::BufferCopy copy{0, 0, size};
    m_uploadCmd.copyBuffer(src.handle, dst.handle, 1, &copy);

    m_uploadCmd.end();

    vk::SubmitInfo si{};
    si.commandBufferCount = 1; si.pCommandBuffers = &m_uploadCmd;
    result = m_graphicsQueue.submit(1, &si, m_uploadFence);
    result = m_device.waitForFences(1, &m_uploadFence, VK_TRUE, UINT64_MAX);
}

Image Renderer::createImage(uint32_t w, uint32_t h, vk::Format fmt, vk::ImageUsageFlags usage, vk::ImageTiling tiling, vk::SampleCountFlagBits samples, uint32_t mipLevels, uint32_t layers, VmaMemoryUsage memUsage) {
    Image out{};
    out.width = w; out.height = h; out.format = fmt; out.mipLevels = mipLevels; out.layers = layers; out.samples = samples;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent = { w, h, 1 };
    ici.mipLevels = mipLevels;
    ici.arrayLayers = layers;
    ici.format = static_cast<VkFormat>(fmt);
    ici.tiling = static_cast<VkImageTiling>(tiling);
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = static_cast<VkImageUsageFlags>(usage);
    ici.samples = static_cast<VkSampleCountFlagBits>(samples);
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = memUsage;

    if (vmaCreateImage(m_allocator, &ici, &aci,
                       reinterpret_cast<VkImage*>(&out.handle), &out.alloc, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Vulkan API: vmaCreateImage failed");
                       }

    vk::ImageViewCreateInfo vci{};
    vci.image = out.handle;
    vci.viewType = vk::ImageViewType::e2D;
    vci.format = fmt;
    vci.subresourceRange = { aspectFromFormat(fmt), 0, mipLevels, 0, layers };
    out.view = m_device.createImageView(vci);

    out.currentLayout = vk::ImageLayout::eUndefined;
    return out;
}

void Renderer::copyBufferToImage(vk::CommandBuffer cmd, Buffer &staging, Image &img, uint32_t w, uint32_t h, uint32_t baseLayer, uint32_t layerCount) {
    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectFromFormat(img.format);
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = baseLayer;
    region.imageSubresource.layerCount = layerCount;
    region.imageOffset = vk::Offset3D{ 0, 0, 0 };
    region.imageExtent = vk::Extent3D{ w, h, 1 };

    cmd.copyBufferToImage(staging.handle, img.handle, vk::ImageLayout::eTransferDstOptimal, 1, &region);
}

void Renderer::generateMipmaps(vk::CommandBuffer cmd, Image &img) {
        if (img.mipLevels <= 1) return;

    int32_t mipWidth  = static_cast<int32_t>(img.width);
    int32_t mipHeight = static_cast<int32_t>(img.height);

    vk::ImageMemoryBarrier2 barrier{};
    barrier.image = img.handle;
    barrier.subresourceRange.aspectMask = aspectFromFormat(img.format);
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = img.layers;
    barrier.subresourceRange.levelCount = 1;

    vk::DependencyInfo dep{};

    for (uint32_t i = 1; i < img.mipLevels; ++i) {
        // Transition src mip (i-1) from TRANSFER_DST -> TRANSFER_SRC
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout     = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstStageMask  = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;

        dep.setImageMemoryBarriers(barrier);
        cmd.pipelineBarrier2(dep);

        // Blit i-1 -> i
        vk::ImageBlit2 blit{};
        blit.srcSubresource = { aspectFromFormat(img.format), i - 1, 0, img.layers };
        blit.srcOffsets[0]  = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1]  = vk::Offset3D{mipWidth, mipHeight, 1};
        blit.dstSubresource = { aspectFromFormat(img.format), i, 0, img.layers };
        blit.dstOffsets[0]  = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1]  = vk::Offset3D{
            std::max(mipWidth  / 2, 1),
            std::max(mipHeight / 2, 1),
            1
        };

        vk::BlitImageInfo2 blitInfo{};
        blitInfo.srcImage       = img.handle;
        blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        blitInfo.dstImage       = img.handle;
        blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        blitInfo.regionCount    = 1;
        blitInfo.pRegions       = &blit;
        blitInfo.filter         = vk::Filter::eLinear;

        cmd.blitImage2(blitInfo);

        // Transition src mip (i-1) from TRANSFER_SRC -> SHADER_READ_ONLY
        barrier.oldLayout     = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;

        dep.setImageMemoryBarriers(barrier);
        cmd.pipelineBarrier2(dep);

        if (mipWidth > 1)  mipWidth  /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition last mip level from TRANSFER_DST -> SHADER_READ_ONLY
    barrier.subresourceRange.baseMipLevel = img.mipLevels - 1;
    barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcStageMask  = vk::PipelineStageFlagBits2::eTransfer;
    barrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;

    dep.setImageMemoryBarriers(barrier);
    cmd.pipelineBarrier2(dep);

    img.currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

vk::Buffer Renderer::uploadVertexBuffer(const void *data, vk::DeviceSize sizeBytes, uint32_t vertexCount) {
    Buffer dst = createBuffer(sizeBytes,
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, false);
    uploadToBuffer(data, sizeBytes, dst, 0);
    //TODO m_ownedBuffers.push_back(dst);
    return dst.handle;
}

vk::Buffer Renderer::uploadIndexBuffer(const uint32_t *data, uint32_t indexCount) {
    const vk::DeviceSize sizeBytes = sizeof(uint32_t) * indexCount;
    Buffer dst = createBuffer(sizeBytes,
        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, false);
    uploadToBuffer(data, sizeBytes, dst, 0);
    //TODO m_ownedBuffers.push_back(dst);
    return dst.handle;
}

void Renderer::uploadSvoBuffers(WorldSvoGpu &gpuWorld) {
    // destroy previous buffer if needed
    if (gpuWorld.svoBuffer.handle) {
        vmaDestroyBuffer(m_allocator, gpuWorld.svoBuffer.handle, gpuWorld.svoBuffer.alloc);
        gpuWorld.svoBuffer = {};
    }
    if (gpuWorld.chunkBuffer.handle) {
        vmaDestroyBuffer(m_allocator, gpuWorld.chunkBuffer.handle, gpuWorld.chunkBuffer.alloc);
        gpuWorld.chunkBuffer = {};
    }

    // Node buffer
    const vk::DeviceSize nodeBytes =
        sizeof(SvoNode) * gpuWorld.globalNodes.size();

    gpuWorld.svoBuffer = createBuffer(
        nodeBytes,
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst,
        0,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );
    uploadToBuffer(gpuWorld.globalNodes.data(), nodeBytes, gpuWorld.svoBuffer);

    // Chunk meta buffer
    const vk::DeviceSize chunkBytes = sizeof(ChunkGpu) * gpuWorld.globalChunks.size();

    gpuWorld.chunkBuffer = createBuffer(
    chunkBytes,
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eTransferDst,
    0,
    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    uploadToBuffer(gpuWorld.globalChunks.data(), chunkBytes, gpuWorld.chunkBuffer);

    std::cout << "SVO Uploaded: " << gpuWorld.globalNodes.size() << " nodes, " << gpuWorld.globalChunks.size() << " chunks.\n";

    uploadMaterialBuffer(gpuWorld);
}

void Renderer::uploadMaterialBuffer(WorldSvoGpu& gpuWorld) {
    // Pack materials from the library
    gpuWorld.materials = m_materialLib.packForGpu();

    // Ensure we have at least one material (the default)
    if (gpuWorld.materials.empty()) {
        // Add default material
        Material defaultMat;
        defaultMat.albedo = glm::vec3(0.8f);
        defaultMat.roughness = 0.5f;
        gpuWorld.materials.push_back(MaterialGpu::pack(defaultMat));
    }

    vk::DeviceSize materialSize = gpuWorld.materials.size() * sizeof(MaterialGpu);

    // Cleanup old buffer if exists
    if (gpuWorld.materialBuffer.handle) {
        vmaDestroyBuffer(m_allocator, gpuWorld.materialBuffer.handle, gpuWorld.materialBuffer.alloc);
        gpuWorld.materialBuffer = {};
    }

    // Create new buffer
    gpuWorld.materialBuffer = createBuffer(
        materialSize,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        0, VMA_MEMORY_USAGE_AUTO
    );

    // Upload data
    uploadToBuffer(gpuWorld.materials.data(), materialSize, gpuWorld.materialBuffer);

    std::cout << "Uploaded material buffer: " << gpuWorld.materials.size()
              << " materials (" << materialSize << " bytes)\n";
}

}