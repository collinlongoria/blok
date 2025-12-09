/*
* File: renderer_draw.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include "image_states.hpp"
#include "renderer.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

namespace blok {

bool resizeNeeded = false;
void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    resizeNeeded = true;
}

void Renderer::render(const Camera& c, float dt) {
    beginFrame();
    renderPerformanceData();
    renderOptionsPanel();
    drawFrame(c, dt);
    endFrame();
}

void Renderer::beginFrame() {
    // reset UBO allocator for this frame
    m_frames[m_frameIndex].uboHead = 0;

    // gui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Renderer::drawFrame(const Camera& c, float dt) {
    // resize if needed
    if (resizeNeeded) {
        m_swapchainDirty = true;
        resizeNeeded = false;

        ImGui::EndFrame();
        return;
    }

    auto& fr = m_frames[m_frameIndex];

    // FrameUBO
    const float aspect = static_cast<float>(m_swapExtent.width) / static_cast<float>(m_swapExtent.height);
    const vk::DeviceSize minAlign = m_physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;

    int depth = 1;
    while (static_cast<float>(rand()) / RAND_MAX < 0.7) depth++;
    depth = std::min(depth, 4);

    // Get base projection
    glm::mat4 baseProj = c.projection(aspect, nearPlane, farPlane);

    // Apply TAA jitter to projection
    glm::mat4 jitteredProj = m_postProcess.getJitteredProjection(baseProj, m_swapExtent.width, m_swapExtent.height);

    FrameUBO fubo{};
    m_denoiser.fillFrameUBO(
        fubo,
        c.view(),
        jitteredProj,
        c.position,
        dt,
        depth,
        m_frameCount,
        m_swapExtent.width,
        m_swapExtent.height,
        0
    );
    glm::vec2 jitter = m_postProcess.getJitterOffset();
    fubo.jitterOffset = jitter;

    m_frameCount++;

    if (c.cameraChanged) {
        c.cameraChanged = false;
    }

    uploadToBuffer(&fubo, sizeof(FrameUBO), fr.frameUBO, 0);
    fr.uboHead = alignUp(sizeof(FrameUBO), minAlign);

    // wait and reset
    if (m_device.waitForFences(1, &fr.inFlight, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
        throw std::runtime_error("waitForFences failed");
    auto result = m_device.resetFences(1, &fr.inFlight);

    // acquire next image
    uint32_t imageIndex = 0;
    const auto acq = m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX, fr.imageAvailable, nullptr, &imageIndex);
    if (acq == vk::Result::eErrorOutOfDateKHR) {
        m_swapchainDirty = true;
        return;
    }
    if (acq != vk::Result::eSuccess && acq != vk::Result::eSuboptimalKHR)
        throw std::runtime_error("acquireNextImageKHR failed");

    if (m_imagesInFlight[imageIndex]) {
        result = m_device.waitForFences(1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    m_imagesInFlight[imageIndex] = fr.inFlight;

    // record
    fr.cmd.reset({});
    vk::CommandBufferBeginInfo bi{};
    fr.cmd.begin(bi);

    ImageTransitions it{ fr.cmd };

    // swapchain image
    Image sw{};
    sw.handle        = m_swapImages[imageIndex];
    sw.view          = m_swapViews[imageIndex];
    sw.width         = m_swapExtent.width;
    sw.height        = m_swapExtent.height;
    sw.mipLevels     = 1;
    sw.layers        = 1;
    sw.format        = m_colorFormat;
    sw.currentLayout = m_swapImageLayouts[imageIndex];

    // Transition G-buffer images to General for ray tracing write
    it.ensure(m_denoiser.gbuffer.color, Role::General);
    it.ensure(m_denoiser.gbuffer.worldPosition, Role::General);
    it.ensure(m_denoiser.gbuffer.normalRoughness, Role::General);
    it.ensure(m_denoiser.gbuffer.albedoMetallic, Role::General);
    it.ensure(m_denoiser.gbuffer.motionVectors, Role::General);

    if (m_world) {
        m_raytracer.updateDescriptorSet(*m_world, m_frameIndex);
    }

    m_raytracer.dispatchRayTracing(fr.cmd, m_swapExtent.width, m_swapExtent.height, m_frameIndex);

    // Memory barrier: ray tracing writes -> compute shader reads
    vk::MemoryBarrier2 rtToComputeBarrier{};
    rtToComputeBarrier.srcStageMask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
    rtToComputeBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    rtToComputeBarrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    rtToComputeBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;

    vk::DependencyInfo rtToComputeDep{};
    rtToComputeDep.memoryBarrierCount = 1;
    rtToComputeDep.pMemoryBarriers = &rtToComputeBarrier;
    fr.cmd.pipelineBarrier2(rtToComputeDep);

    // Before denoising - ensure history images are in correct layout
    it.ensure(m_denoiser.gbuffer.previousWorldPosition(), Role::General);
    it.ensure(m_denoiser.gbuffer.previousNormalRoughness(), Role::General);

    // Run temporal reprojection compute shader
    m_denoiser.updateDescriptorSets(m_frameIndex);
    m_denoiser.denoise(fr.cmd, m_swapExtent.width, m_swapExtent.height, m_frameIndex);

    // Memory barrier: compute shader writes -> transfer reads
    vk::MemoryBarrier2 computeToTransferBarrier{};
    computeToTransferBarrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    computeToTransferBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    computeToTransferBarrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
    computeToTransferBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;

    vk::DependencyInfo computeToTransferDep{};
    computeToTransferDep.memoryBarrierCount = 1;
    computeToTransferDep.pMemoryBarriers = &computeToTransferBarrier;
    fr.cmd.pipelineBarrier2(computeToTransferDep);

    // Transition images for copy
    it.ensure(m_denoiser.gbuffer.worldPosition, Role::TransferSrc);
    it.ensure(m_denoiser.gbuffer.normalRoughness, Role::TransferSrc);
    it.ensure(m_denoiser.gbuffer.currentWorldPosition(), Role::TransferDst);
    it.ensure(m_denoiser.gbuffer.currentNormalRoughness(), Role::TransferDst);

    // Copy current frame's geometry to history (will become "previous" after swap)
    m_denoiser.copyCurrentGeometryToHistory(fr.cmd);

    // Memory barrier: transfer -> compute for post-processing
    vk::MemoryBarrier2 transferToComputeBarrier{};
    transferToComputeBarrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    transferToComputeBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    transferToComputeBarrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    transferToComputeBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;

    vk::DependencyInfo transferToComputeDep{};
    transferToComputeDep.memoryBarrierCount = 1;
    transferToComputeDep.pMemoryBarriers = &transferToComputeBarrier;
    fr.cmd.pipelineBarrier2(transferToComputeDep);

    // ==================== POST-PROCESSING (TAA + Tonemap + Sharpen) ====================
    Image& denoisedOutput = m_denoiser.getOutputImage();
    m_postProcess.process(fr.cmd, denoisedOutput, m_swapExtent.width, m_swapExtent.height, m_frameIndex);

    // Memory barrier: post-process compute -> transfer for blit
    vk::MemoryBarrier2 postToTransferBarrier{};
    postToTransferBarrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    postToTransferBarrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    postToTransferBarrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
    postToTransferBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;

    vk::DependencyInfo postToTransferDep{};
    postToTransferDep.memoryBarrierCount = 1;
    postToTransferDep.pMemoryBarriers = &postToTransferBarrier;
    fr.cmd.pipelineBarrier2(postToTransferDep);

    // Get final post-processed output for blit
    Image& finalOutput = m_postProcess.getOutputImage();
    it.ensure(finalOutput, Role::TransferSrc);
    it.ensure(sw, Role::TransferDst);

    // Blit post-processed output to swapchain
    vk::ImageBlit blit{};
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.srcSubresource.mipLevel   = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount     = 1;
    blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.srcOffsets[1] = vk::Offset3D{
        static_cast<int>(finalOutput.width),
        static_cast<int>(finalOutput.height),
        1
    };

    blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.dstSubresource.mipLevel   = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount     = 1;
    blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.dstOffsets[1] = vk::Offset3D{
        static_cast<int>(m_swapExtent.width),
        static_cast<int>(m_swapExtent.height),
        1
    };

    fr.cmd.blitImage(
        finalOutput.handle, vk::ImageLayout::eTransferSrcOptimal,
        sw.handle, vk::ImageLayout::eTransferDstOptimal,
        1, &blit, vk::Filter::eLinear
    );

    // Transition swapchain image for gui rendering
    it.ensure(sw, Role::ColorAttachment);
/*
    const std::array<float,4> clear{0.0f,0.0f,0.0f,1.0f};
    cmdBeginRendering(fr.cmd, sw.view, m_depth.view, m_swapExtent, clear, 1.0f, 0);
    // Can do render stuff here
    cmdEndRendering(fr.cmd);
*/

    // imgui
    {
        vk::RenderingAttachmentInfo uiColor{};
        uiColor.imageView   = sw.view;
        uiColor.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        uiColor.loadOp      = vk::AttachmentLoadOp::eLoad;   // keep scene
        uiColor.storeOp     = vk::AttachmentStoreOp::eStore;
        // clearValue ignored with eLoad

        vk::RenderingInfo uiInfo{};
        uiInfo.renderArea        = vk::Rect2D({0, 0}, m_swapExtent);
        uiInfo.layerCount        = 1;
        uiInfo.colorAttachmentCount = 1;
        uiInfo.pColorAttachments = &uiColor;
        uiInfo.pDepthAttachment  = nullptr;
        uiInfo.pStencilAttachment= nullptr;

        fr.cmd.beginRendering(uiInfo);

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(fr.cmd), VK_NULL_HANDLE);

        fr.cmd.endRendering();
    }

    it.ensure(sw, Role::Present);
    m_swapImageLayouts[imageIndex] = sw.currentLayout;

    fr.cmd.end();

    // submit
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo si{};
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &fr.imageAvailable;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &fr.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &m_presentSignals[imageIndex];

    result = m_graphicsQueue.submit(1, &si, fr.inFlight);

    vk::PresentInfoKHR pi{};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &m_presentSignals[imageIndex];
    vk::SwapchainKHR scH  = m_swapchain;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &scH;
    pi.pImageIndices      = &imageIndex;

    const auto pres = m_presentQueue.presentKHR(pi);
    if (pres == vk::Result::eErrorOutOfDateKHR || pres == vk::Result::eSuboptimalKHR)
        m_swapchainDirty = true;
    else if (pres != vk::Result::eSuccess)
        throw std::runtime_error("presentKHR failed");

    // Store current frame's camera data for next frame's reprojection
    m_denoiser.updatePreviousFrameData(
        c.view(),
        baseProj,  // Use NON-jittered projection for reprojection
        c.position
    );

    // Store previous frame data for TAA
    m_postProcess.updatePreviousFrameData(
        c.view(),
        baseProj  // Use NON-jittered projection
    );

    // Swap history buffers (current becomes previous for next frame)
    m_denoiser.swapHistoryBuffers();
    m_postProcess.swapHistoryBuffers();
}

void Renderer::cmdBeginRendering(vk::CommandBuffer cmd, vk::ImageView colorView, vk::ImageView depthView, vk::Extent2D extent, const std::array<float, 4> &clearColor, float clearDepth, uint32_t clearStencil) {
    vk::ClearValue cv{};
    cv.color = vk::ClearColorValue{ std::array<float, 4>{clearColor[0], clearColor[1], clearColor[2], clearColor[3] } };

    vk::ClearValue dv{};
    dv.depthStencil = vk::ClearDepthStencilValue{ clearDepth, clearStencil };

    vk::RenderingAttachmentInfo color{};
    color.imageView = colorView;
    color.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    color.loadOp = vk::AttachmentLoadOp::eClear;
    color.storeOp = vk::AttachmentStoreOp::eStore;
    color.clearValue = cv;

    vk::RenderingAttachmentInfo depth{};
    depth.imageView = depthView;
    depth.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depth.loadOp = vk::AttachmentLoadOp::eClear;
    depth.storeOp = vk::AttachmentStoreOp::eDontCare;
    depth.clearValue = dv;

    vk::RenderingInfo info{};
    info.renderArea = vk::Rect2D{ {0,0}, extent };
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    info.pDepthAttachment = &depth;

    cmd.beginRendering(info);

    // this is a dynamic viewport/scissor covering whole target
    vk::Viewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    vk::Rect2D sc{{0,0}, extent};
    cmd.setViewport(0, 1, &vp);
    cmd.setScissor(0, 1, &sc);
}

void Renderer::cmdEndRendering(vk::CommandBuffer cmd) {
    cmd.endRendering();
}

void Renderer::endFrame() {
    // advance frame
    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;

    if (m_swapchainDirty) {
        recreateSwapChain();
        m_swapchainDirty = false;
    }
}


}