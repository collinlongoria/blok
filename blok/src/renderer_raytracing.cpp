/*
* File: renderer_raytracing.cpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#include "renderer_raytracing.hpp"

#include <iostream>

#include "renderer.hpp"

namespace blok {

vk::AccelerationStructureKHR Renderer::buildChunkBlas(WorldSvoGpu &gpuWorld) {
    // count primitives
    uint32_t count = gpuWorld.globalSubChunks.size();
    if (count == 0) return {};

    // build array of aabbs
    std::vector<vk::AabbPositionsKHR> aabbs(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& sub = gpuWorld.globalSubChunks[i];
        aabbs[i].minX = sub.worldMin.x;
        aabbs[i].minY = sub.worldMin.y;
        aabbs[i].minZ = sub.worldMin.z;
        aabbs[i].maxX = sub.worldMax.x;
        aabbs[i].maxY = sub.worldMax.y;
        aabbs[i].maxZ = sub.worldMax.z;
    }

    // clean up old AABB buffer if it exists
    if (gpuWorld.blasAabbBuffer.handle) {
        vmaDestroyBuffer(m_allocator, gpuWorld.blasAabbBuffer.handle, gpuWorld.blasAabbBuffer.alloc);
        gpuWorld.blasAabbBuffer = {};
    }

    // create aabb buffer
    gpuWorld.blasAabbBuffer = createBuffer(
        sizeof(vk::AabbPositionsKHR) * count,
        vk::BufferUsageFlagBits::eShaderDeviceAddress |
        vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst,
        0, VMA_MEMORY_USAGE_AUTO
    );
    uploadToBuffer(aabbs.data(), sizeof(aabbs[0]) * count, gpuWorld.blasAabbBuffer);

    vk::BufferDeviceAddressInfo ai{gpuWorld.blasAabbBuffer.handle};
    vk::DeviceAddress addr = m_device.getBufferAddress(ai);

    // build blas geo info
    vk::AccelerationStructureGeometryKHR geom{};
    geom.geometryType = vk::GeometryTypeKHR::eAabbs;
    geom.flags = vk::GeometryFlagBitsKHR::eOpaque;
    geom.geometry.setAabbs({addr, sizeof(vk::AabbPositionsKHR)});

    vk::AccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = count;
    range.primitiveOffset = 0;
    range.firstVertex = 0;
    range.transformOffset = 0;

    vk::AccelerationStructureBuildGeometryInfoKHR build{};
    build.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    build.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    build.setGeometries(geom);
    build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;

    // query sizes
    auto sizes = m_device.getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice,
        build,
        range.primitiveCount
    );

    // cleanup old BLAS if it exists
    if (gpuWorld.blas.handle) {
        m_device.destroyAccelerationStructureKHR(gpuWorld.blas.handle);
        gpuWorld.blas.handle = nullptr;
    }
    if (gpuWorld.blas.buffer.handle) {
        vmaDestroyBuffer(m_allocator, gpuWorld.blas.buffer.handle, gpuWorld.blas.buffer.alloc);
        gpuWorld.blas.buffer = {};
    }

    // create blas buffer + as handle
    gpuWorld.blas.buffer = createBuffer(
            sizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
            );

    vk::AccelerationStructureCreateInfoKHR ci{};
    ci.buffer = gpuWorld.blas.buffer.handle;
    ci.size = sizes.accelerationStructureSize;
    ci.type = vk::AccelerationStructureTypeKHR::eBottomLevel;

    gpuWorld.blas.handle = m_device.createAccelerationStructureKHR(ci);

    // scratch buffer
    Buffer scratch = createBuffer(
        sizes.buildScratchSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eShaderDeviceAddress,
        0, VMA_MEMORY_USAGE_AUTO
    );

    vk::DeviceAddress scratchAddr =
        m_device.getBufferAddress({ scratch.handle });

    build.dstAccelerationStructure = gpuWorld.blas.handle;
    build.scratchData.deviceAddress = scratchAddr;

    // build blas
    m_uploadCmd.reset({});
    m_uploadCmd.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    const vk::AccelerationStructureBuildRangeInfoKHR* pRange = &range;
    m_uploadCmd.buildAccelerationStructuresKHR(
        build,
        pRange
    );

    m_uploadCmd.end();
    vk::SubmitInfo submitInfo({}, {}, m_uploadCmd, {});
    m_graphicsQueue.submit(submitInfo, {});
    m_graphicsQueue.waitIdle();

    vmaDestroyBuffer(m_allocator, scratch.handle, scratch.alloc);

    std::cout << "BLAS built with " << count << " sub-chunk AABBs\n";

    return gpuWorld.blas.handle;
}

vk::AccelerationStructureKHR Renderer::buildChunkTlas(WorldSvoGpu &gpuWorld) {
    uint32_t subChunkCount = gpuWorld.globalSubChunks.size();
    if (subChunkCount == 0)
        return {};

    vk::AccelerationStructureInstanceKHR inst{};
    inst.accelerationStructureReference =
        m_device.getAccelerationStructureAddressKHR({ gpuWorld.blas.handle });

    inst.instanceCustomIndex = 0;  // Not used since gl_PrimitiveID gives us the AABB index
    inst.mask = 0xFF;
    inst.instanceShaderBindingTableRecordOffset = 0;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    // Identity transform (AABBs are already in world space)
    std::array<std::array<float, 4>, 3> t = {{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}
    }};
    inst.transform = vk::TransformMatrixKHR{t};

    // Cleanup old instance buffer if it exists
    if (gpuWorld.tlasInstanceBuffer.handle) {
        vmaDestroyBuffer(m_allocator, gpuWorld.tlasInstanceBuffer.handle, gpuWorld.tlasInstanceBuffer.alloc);
        gpuWorld.tlasInstanceBuffer = {};
    }

    // Create instance buffer
    gpuWorld.tlasInstanceBuffer = createBuffer(
        sizeof(vk::AccelerationStructureInstanceKHR),
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eShaderDeviceAddress |
        vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
        0, VMA_MEMORY_USAGE_AUTO
    );
    uploadToBuffer(&inst, sizeof(inst), gpuWorld.tlasInstanceBuffer);

    vk::DeviceAddress instAddr = m_device.getBufferAddress({ gpuWorld.tlasInstanceBuffer.handle });

    // Setup TLAS geometry
    vk::AccelerationStructureGeometryInstancesDataKHR instances{};
    instances.arrayOfPointers = VK_FALSE;
    instances.data.deviceAddress = instAddr;

    vk::AccelerationStructureGeometryKHR geom{};
    geom.geometryType = vk::GeometryTypeKHR::eInstances;
    geom.geometry.setInstances(instances);

    vk::AccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = 1;

    vk::AccelerationStructureBuildGeometryInfoKHR build{};
    build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    build.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    build.setGeometries(geom);
    build.mode = vk::BuildAccelerationStructureModeKHR::eBuild;

    auto sizes = m_device.getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice,
        build,
        range.primitiveCount
    );

    // Cleanup old TLAS if it exists
    if (gpuWorld.tlas.handle) {
        m_device.destroyAccelerationStructureKHR(gpuWorld.tlas.handle);
        gpuWorld.tlas.handle = nullptr;
    }
    if (gpuWorld.tlas.buffer.handle) {
        vmaDestroyBuffer(m_allocator, gpuWorld.tlas.buffer.handle, gpuWorld.tlas.buffer.alloc);
        gpuWorld.tlas.buffer = {};
    }

    // Create TLAS buffer
    gpuWorld.tlas.buffer = createBuffer(
        sizes.accelerationStructureSize,
        vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
        vk::BufferUsageFlagBits::eShaderDeviceAddress,
        0, VMA_MEMORY_USAGE_AUTO
    );

    vk::AccelerationStructureCreateInfoKHR ci{};
    ci.buffer = gpuWorld.tlas.buffer.handle;
    ci.size = sizes.accelerationStructureSize;
    ci.type = vk::AccelerationStructureTypeKHR::eTopLevel;

    gpuWorld.tlas.handle = m_device.createAccelerationStructureKHR(ci);

    // Scratch buffer
    Buffer scratch = createBuffer(
        sizes.buildScratchSize,
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eShaderDeviceAddress,
        0, VMA_MEMORY_USAGE_AUTO
    );

    vk::DeviceAddress scratchAddr = m_device.getBufferAddress({ scratch.handle });

    build.dstAccelerationStructure = gpuWorld.tlas.handle;
    build.scratchData.deviceAddress = scratchAddr;

    // Build TLAS
    m_uploadCmd.reset({});
    m_uploadCmd.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

    const vk::AccelerationStructureBuildRangeInfoKHR* pRange = &range;
    m_uploadCmd.buildAccelerationStructuresKHR(build, pRange);

    m_uploadCmd.end();
    vk::SubmitInfo submitInfo({}, {}, m_uploadCmd, {});
    m_graphicsQueue.submit(submitInfo, {});
    m_graphicsQueue.waitIdle();

    vmaDestroyBuffer(m_allocator, scratch.handle, scratch.alloc);

    return gpuWorld.tlas.handle;
}

RayTracing::RayTracing(Renderer* r_)
    :r(r_) {}

void RayTracing::createDescriptorSetLayout() {
    // 0 = TLAS
    vk::DescriptorSetLayoutBinding tlas{};
    tlas.binding = 0;
    tlas.descriptorCount = 1;
    tlas.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    tlas.stageFlags =
        vk::ShaderStageFlagBits::eRaygenKHR |
        vk::ShaderStageFlagBits::eClosestHitKHR |
        vk::ShaderStageFlagBits::eIntersectionKHR;

    // 1 = SVO
    vk::DescriptorSetLayoutBinding svoBuf{};
    svoBuf.binding = 1;
    svoBuf.descriptorCount = 1;
    svoBuf.descriptorType = vk::DescriptorType::eStorageBuffer;
    svoBuf.stageFlags =
        vk::ShaderStageFlagBits::eRaygenKHR |
        vk::ShaderStageFlagBits::eClosestHitKHR |
        vk::ShaderStageFlagBits::eIntersectionKHR;

    // 2 = Chunk metadata
    vk::DescriptorSetLayoutBinding chunkBuf{};
    chunkBuf.binding = 2;
    chunkBuf.descriptorCount = 1;
    chunkBuf.descriptorType = vk::DescriptorType::eStorageBuffer;
    chunkBuf.stageFlags = svoBuf.stageFlags;

    // 3 = Frame UBO
    vk::DescriptorSetLayoutBinding frameUBO{};
    frameUBO.binding = 3;
    frameUBO.descriptorCount = 1;
    frameUBO.descriptorType = vk::DescriptorType::eUniformBuffer;
    frameUBO.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    // 4 = Output Image
    vk::DescriptorSetLayoutBinding outImg{};
    outImg.binding = 4;
    outImg.descriptorCount = 1;
    outImg.descriptorType = vk::DescriptorType::eStorageImage;
    outImg.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    // 5 = World Position Output
    vk::DescriptorSetLayoutBinding wp{};
    wp.binding = 5;
    wp.descriptorCount = 1;
    wp.descriptorType = vk::DescriptorType::eStorageImage;
    wp.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    // 6 = Normal + Roughness Output
    vk::DescriptorSetLayoutBinding nr{};
    nr.binding = 6;
    nr.descriptorCount = 1;
    nr.descriptorType = vk::DescriptorType::eStorageImage;
    nr.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    // 7 = Albedo + Metallic Output
    vk::DescriptorSetLayoutBinding am{};
    am.binding = 7;
    am.descriptorCount = 1;
    am.descriptorType = vk::DescriptorType::eStorageImage;
    am.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    // 8 = Motion Vectors
    vk::DescriptorSetLayoutBinding mv{};
    mv.binding = 8;
    mv.descriptorCount = 1;
    mv.descriptorType = vk::DescriptorType::eStorageImage;
    mv.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    // 9 = Material Buffer
    vk::DescriptorSetLayoutBinding mb{};
    mb.binding = 9;
    mb.descriptorCount = 1;
    mb.descriptorType = vk::DescriptorType::eStorageBuffer;
    mb.stageFlags = vk::ShaderStageFlagBits::eClosestHitKHR;

    std::array<vk::DescriptorSetLayoutBinding, 10> bindings =
    { tlas, svoBuf, chunkBuf, frameUBO, outImg, wp, nr, am, mv, mb };

    vk::DescriptorSetLayoutCreateInfo ci{};
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();

    rtSetLayout = r->m_device.createDescriptorSetLayout(ci);
}

void RayTracing::allocateDescriptorSet() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rtSets[i] = r->m_descAlloc.allocate(r->m_device, rtSetLayout);
    }
}

void RayTracing::updateDescriptorSet(const WorldSvoGpu& gpu, uint32_t frameIndex)
{
    auto& gbuffer = r->m_denoiser.gbuffer;
    vk::DescriptorSet currentSet = rtSets[frameIndex];

    // Acceleration structure
    vk::WriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures = &gpu.tlas.handle;

    vk::WriteDescriptorSet asWrite{};
    asWrite.dstSet = currentSet;
    asWrite.dstBinding = 0;
    asWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    asWrite.descriptorCount = 1;
    asWrite.pNext = &asInfo;

    // SVO SSBO
    vk::DescriptorBufferInfo svoInfo{
        gpu.svoBuffer.handle,
        0,
        VK_WHOLE_SIZE
    };

    vk::WriteDescriptorSet svoWrite{};
    svoWrite.dstSet = currentSet;
    svoWrite.dstBinding = 1;
    svoWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    svoWrite.setBufferInfo(svoInfo);

    // Chunk SSBO
    vk::DescriptorBufferInfo chunkInfo{
        gpu.subChunkBuffer.handle,
        0, VK_WHOLE_SIZE
    };

    vk::WriteDescriptorSet chunkWrite{};
    chunkWrite.dstSet = currentSet;
    chunkWrite.dstBinding = 2;
    chunkWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    chunkWrite.setBufferInfo(chunkInfo);

    // Frame UBO
    auto& fr = r->m_frames[r->m_frameIndex];
    vk::DescriptorBufferInfo frameInfo{
        fr.frameUBO.handle,
        0,
        VK_WHOLE_SIZE
    };

    vk::WriteDescriptorSet frameWrite{};
    frameWrite.dstSet = currentSet;
    frameWrite.dstBinding = 3;
    frameWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
    frameWrite.setBufferInfo(frameInfo);

    // Output Image
    vk::DescriptorImageInfo imgInfo{
        nullptr,
        gbuffer.color.view,
        vk::ImageLayout::eGeneral
    };

    vk::WriteDescriptorSet imgWrite{};
    imgWrite.dstSet = currentSet;
    imgWrite.dstBinding = 4;
    imgWrite.descriptorType = vk::DescriptorType::eStorageImage;
    imgWrite.setImageInfo(imgInfo);

    // Temporal Reprojection
    vk::DescriptorImageInfo wpInfo{
        nullptr,
        gbuffer.worldPosition.view,
        vk::ImageLayout::eGeneral
    };
    vk::WriteDescriptorSet wpWrite{};
    wpWrite.dstSet = currentSet;
    wpWrite.dstBinding = 5;
    wpWrite.descriptorType = vk::DescriptorType::eStorageImage;
    wpWrite.setImageInfo(wpInfo);

    vk::DescriptorImageInfo nrInfo{
        nullptr,
        gbuffer.normalRoughness.view,
        vk::ImageLayout::eGeneral
    };
    vk::WriteDescriptorSet nrWrite{};
    nrWrite.dstSet = currentSet;
    nrWrite.dstBinding = 6;
    nrWrite.descriptorType = vk::DescriptorType::eStorageImage;
    nrWrite.setImageInfo(nrInfo);

    vk::DescriptorImageInfo amInfo{
        nullptr,
        gbuffer.albedoMetallic.view,
        vk::ImageLayout::eGeneral
    };
    vk::WriteDescriptorSet amWrite{};
    amWrite.dstSet = currentSet;
    amWrite.dstBinding = 7;
    amWrite.descriptorType = vk::DescriptorType::eStorageImage;
    amWrite.setImageInfo(amInfo);

    vk::DescriptorImageInfo motionInfo{
        nullptr,
        r->m_denoiser.gbuffer.motionVectors.view,
        vk::ImageLayout::eGeneral
    };
    vk::WriteDescriptorSet motionWrite{};
    motionWrite.dstSet = currentSet;
    motionWrite.dstBinding = 8;
    motionWrite.descriptorType = vk::DescriptorType::eStorageImage;
    motionWrite.setImageInfo(motionInfo);

    // Material buffer
    vk::DescriptorBufferInfo materialInfo{};
    materialInfo.buffer = r->m_world->materialBuffer.handle;
    materialInfo.offset = 0;
    materialInfo.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet materialWrite{};
    materialWrite.dstSet = currentSet;
    materialWrite.dstBinding = 9;
    materialWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    materialWrite.descriptorCount = 1;
    materialWrite.pBufferInfo = &materialInfo;

    std::array<vk::WriteDescriptorSet,10> writes =
    { asWrite, svoWrite, chunkWrite, frameWrite, imgWrite, wpWrite, nrWrite, amWrite, motionWrite, materialWrite };

    r->m_device.updateDescriptorSets(writes, {});
}

void RayTracing::createPipeline() {
    auto load = [&](const std::string& name, vk::ShaderStageFlagBits stage)
    {
        return r->m_shaderManager.loadModule("assets/shaders/" + name, stage);
    };

    vk::ShaderModule rgen = load("raygen.rgen", vk::ShaderStageFlagBits::eRaygenKHR).module;
    vk::ShaderModule miss = load("miss.rmiss", vk::ShaderStageFlagBits::eMissKHR).module;
    vk::ShaderModule missShadow = load("shadow.rmiss", vk::ShaderStageFlagBits::eMissKHR).module;
    vk::ShaderModule isect = load("intersect.rint", vk::ShaderStageFlagBits::eIntersectionKHR).module;
    vk::ShaderModule chit = load("hit.rchit", vk::ShaderStageFlagBits::eClosestHitKHR).module;

    // Shader stages
    std::vector<vk::PipelineShaderStageCreateInfo> stages = {
    { {}, vk::ShaderStageFlagBits::eRaygenKHR, rgen, "main" },
    { {}, vk::ShaderStageFlagBits::eMissKHR, miss, "main" },
    { {}, vk::ShaderStageFlagBits::eMissKHR, missShadow, "main" },
    { {}, vk::ShaderStageFlagBits::eIntersectionKHR, isect, "main" },
    { {}, vk::ShaderStageFlagBits::eClosestHitKHR, chit, "main" }
    };

    // Shader groups
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups;

    // group 0 = raygen
    groups.push_back(
        vk::RayTracingShaderGroupCreateInfoKHR{}
        .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
        .setGeneralShader(0)
        .setClosestHitShader(VK_SHADER_UNUSED_KHR)
        .setAnyHitShader(VK_SHADER_UNUSED_KHR)
        .setIntersectionShader(VK_SHADER_UNUSED_KHR)
    );

    // group 1 = miss
    groups.push_back(
        vk::RayTracingShaderGroupCreateInfoKHR{}
        .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
        .setGeneralShader(1)
        .setClosestHitShader(VK_SHADER_UNUSED_KHR)
        .setAnyHitShader(VK_SHADER_UNUSED_KHR)
        .setIntersectionShader(VK_SHADER_UNUSED_KHR)
    );

    // group 2: miss shadow
    groups.push_back(
        vk::RayTracingShaderGroupCreateInfoKHR{}
        .setType(vk::RayTracingShaderGroupTypeKHR::eGeneral)
        .setGeneralShader(2)
        .setClosestHitShader(VK_SHADER_UNUSED_KHR)
        .setAnyHitShader(VK_SHADER_UNUSED_KHR)
        .setIntersectionShader(VK_SHADER_UNUSED_KHR)
    );

    // group 3 = hit group (regular)
    groups.push_back(
        vk::RayTracingShaderGroupCreateInfoKHR{}
        .setType(vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup)
        .setGeneralShader(VK_SHADER_UNUSED_KHR)
        .setIntersectionShader(3)
        .setClosestHitShader(4)
        .setAnyHitShader(VK_SHADER_UNUSED_KHR)
    );

    // group 4: hit group for shadows
    groups.push_back(
        vk::RayTracingShaderGroupCreateInfoKHR{}
        .setType(vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup)
        .setGeneralShader(VK_SHADER_UNUSED_KHR)
        .setIntersectionShader(3)
        .setClosestHitShader(VK_SHADER_UNUSED_KHR)
        .setAnyHitShader(VK_SHADER_UNUSED_KHR)
    );

    // Layout
    vk::PipelineLayoutCreateInfo lci{};
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &rtSetLayout;

    rtPipeline.layout = r->m_device.createPipelineLayout(lci);

    // Pipeline
    vk::RayTracingPipelineCreateInfoKHR pci{};
    pci.stageCount = stages.size();
    pci.pStages = stages.data();
    pci.groupCount = groups.size();
    pci.pGroups = groups.data();
    pci.maxPipelineRayRecursionDepth = 10;
    pci.layout = rtPipeline.layout;

    auto res = r->m_device.createRayTracingPipelinesKHR(
    nullptr, nullptr, pci);

    rtPipeline.pipeline = res.value[0];

    r->m_device.destroyShaderModule(rgen);
    r->m_device.destroyShaderModule(miss);
    r->m_device.destroyShaderModule(isect);
    r->m_device.destroyShaderModule(chit);
    r->m_device.destroyShaderModule(missShadow);
}

void RayTracing::createSBT() {
    auto props = r->m_rtProps;

    const uint32_t handleSize         = props.shaderGroupHandleSize;         // e.g. 32
    const uint32_t baseAlignment      = props.shaderGroupBaseAlignment;      // e.g. 64
    const uint32_t handleSizeAligned  = (handleSize + baseAlignment - 1) & ~(baseAlignment - 1);

    const uint32_t groupCount = 5; // rgen, miss, hit

    std::vector<uint8_t> handles(groupCount * handleSize);

    auto result = r->m_device.getRayTracingShaderGroupHandlesKHR(
        rtPipeline.pipeline,
        0, groupCount,
        handles.size(),
        handles.data()
    );

    auto makeSBT = [&](Buffer& buf,
                       vk::StridedDeviceAddressRegionKHR& region,
                       uint32_t index,
                       uint32_t count)
    {
        // SBT BUFFER SIZE MUST BE handleSizeAligned
        const uint32_t sbtSize = handleSizeAligned * count;

        buf = r->createBuffer(
            sbtSize,
            vk::BufferUsageFlagBits::eShaderBindingTableKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress |
            vk::BufferUsageFlagBits::eTransferDst,
            0,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        // We must align the data we copy into the buffer as well
        std::vector<uint8_t> alignedHandles(sbtSize);
        for(uint32_t i = 0; i < count; i++) {
            // Copy handle from the main vector to the aligned position
            memcpy(
                alignedHandles.data() + (i * handleSizeAligned),
                handles.data() + ((index + i) * handleSize),
                handleSize
            );
        }

        r->uploadToBuffer(
                    alignedHandles.data(),
                    sbtSize,
                    buf
                );

        vk::DeviceAddress addr =
            r->m_device.getBufferAddress({ buf.handle });

        // MUST BE aligned to shaderGroupBaseAlignment
        if (addr % baseAlignment != 0) {
            throw std::runtime_error("SBT deviceAddress is not aligned!");
        }

        region = vk::StridedDeviceAddressRegionKHR{
            addr,
            handleSizeAligned,  // Stride (distance between records)
            sbtSize             // Size (total size of the region) <--- FIXED
        };
    };

    // Raygen
    makeSBT(rtPipeline.rgenSBT, rtPipeline.rgenRegion, 0, 1);

    // Miss (Radiance Miss, Shadow Miss)
    makeSBT(rtPipeline.missSBT, rtPipeline.missRegion, 1, 2);

    //Hit (Radiance Hit, Shadow Hit)
    makeSBT(rtPipeline.hitSBT,  rtPipeline.hitRegion,  3, 2);

    rtPipeline.callRegion = vk::StridedDeviceAddressRegionKHR{};
}

void RayTracing::dispatchRayTracing(vk::CommandBuffer cmd, uint32_t w, uint32_t h, uint32_t frameIndex) {
    cmd.bindPipeline(
        vk::PipelineBindPoint::eRayTracingKHR,
        rtPipeline.pipeline
    );

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR,
        rtPipeline.layout,
        0, rtSets[frameIndex], {}
    );

    cmd.traceRaysKHR(
        rtPipeline.rgenRegion,
        rtPipeline.missRegion,
        rtPipeline.hitRegion,
        rtPipeline.callRegion,
        w, h, 1
    );
}

}