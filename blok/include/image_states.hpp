/*
* File: image_states.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef IMAGE_STATES_HPP
#define IMAGE_STATES_HPP
#include "vulkan_context.hpp"
#include "resources.hpp"

namespace blok {

enum class Role { General, StorageWrite, Sampled, ColorAttachment, DepthAttachment, Present, TransferDst, TransferSrc};

struct ImageState {
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
};

class ImageTransitions {
public:
    explicit ImageTransitions(vk::CommandBuffer cmd) : cmd(cmd) {}

    void ensure(Image& img, Role dst) {
        auto desired = toLayout(dst);
        if (img.currentLayout == desired.layout) return;

        vk::ImageMemoryBarrier2 b{};
        b.oldLayout = img.currentLayout;
        b.newLayout = desired.layout;
        b.srcStageMask = stagesFor(img.currentLayout);
        b.srcAccessMask = accessFor(img.currentLayout);
        b.dstStageMask = stagesFor(desired.layout);
        b.dstAccessMask = accessFor(desired.layout);
        b.image = img.handle;
        b.subresourceRange = { desired.aspect, 0, img.mipLevels, 0, img.layers };

        vk::DependencyInfo dep{}; dep.setImageMemoryBarriers(b);
        cmd.pipelineBarrier2(dep);
        img.currentLayout = desired.layout;
    }

private:
    vk::CommandBuffer cmd;

    static ImageState toLayout(Role r) {
        switch (r) {
            case Role::General: return {vk::ImageLayout::eGeneral, vk::ImageAspectFlagBits::eColor};
            case Role::StorageWrite: return {vk::ImageLayout::eGeneral, vk::ImageAspectFlagBits::eColor};
            case Role::Sampled: return {vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor};
            case Role::ColorAttachment: return {vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor};
            case Role::DepthAttachment: return {vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageAspectFlagBits::eDepth};
            case Role::Present: return {vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor};
            case Role::TransferDst: return {vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor};
            case Role::TransferSrc: return {vk::ImageLayout::eTransferSrcOptimal, vk::ImageAspectFlagBits::eColor};
        }
        return {};
    }

    static vk::PipelineStageFlags2 stagesFor(vk::ImageLayout l){
        if (l==vk::ImageLayout::eUndefined) return vk::PipelineStageFlagBits2::eTopOfPipe;
        if (l==vk::ImageLayout::eGeneral) return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        if (l==vk::ImageLayout::eShaderReadOnlyOptimal) return vk::PipelineStageFlagBits2::eFragmentShader;
        if (l==vk::ImageLayout::eColorAttachmentOptimal) return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        if (l==vk::ImageLayout::eDepthAttachmentOptimal) return vk::PipelineStageFlagBits2::eEarlyFragmentTests;
        if (l==vk::ImageLayout::eTransferDstOptimal || l==vk::ImageLayout::eTransferSrcOptimal) return vk::PipelineStageFlagBits2::eTransfer;
        if (l==vk::ImageLayout::ePresentSrcKHR) return vk::PipelineStageFlagBits2::eBottomOfPipe;
        return vk::PipelineStageFlagBits2::eAllCommands;
    }

    static vk::AccessFlags2 accessFor(vk::ImageLayout l){
        if (l==vk::ImageLayout::eUndefined) return {};
        if (l==vk::ImageLayout::eGeneral) return vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead;
        if (l==vk::ImageLayout::eShaderReadOnlyOptimal) return vk::AccessFlagBits2::eShaderRead;
        if (l==vk::ImageLayout::eColorAttachmentOptimal) return vk::AccessFlagBits2::eColorAttachmentWrite;
        if (l==vk::ImageLayout::eDepthAttachmentOptimal) return vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
        if (l==vk::ImageLayout::eTransferDstOptimal) return vk::AccessFlagBits2::eTransferWrite;
        if (l==vk::ImageLayout::eTransferSrcOptimal) return vk::AccessFlagBits2::eTransferRead;
        if (l==vk::ImageLayout::ePresentSrcKHR) return {};
        return {};
    }
};

}

#endif //IMAGE_STATES_HPP