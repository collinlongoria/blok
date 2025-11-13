/*
* File: vulkan_acceleration
* Project: blok
* Author: Collin Longoria
* Created on: 11/12/2025
*
* Description:
*/

#include "vulkan_acceleration.hpp"


using namespace blok;

void RaytracingBuilder::init(VulkanRenderer *vk, const vk::Device &device, uint32_t queueIndex) {
    VK = vk;
    m_device = device;
    m_queueIndex = queueIndex;
}

void RaytracingBuilder::destroy() {
    for (auto& blas : m_blas) {

    }

    m_blas.clear();
}

vk::AccelerationStructureKHR RaytracingBuilder::getAccelerationStructure() const {
    return m_tlas.acceleration;
}

vk::DeviceAddress RaytracingBuilder::getBlasDeviceAddress(uint32_t blasId) {
    assert(static_cast<size_t>(blasId) < m_blas.size());

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure = m_blas[blasId].acceleration;

    return vkGetAccelerationStructureDeviceAddressKHR(m_device, &addressInfo);
}

void RaytracingBuilder::buildBlas(const std::vector<BlasInput> &input, vk::BuildAccelerationStructureFlagsKHR flags) {
    auto nbBlas = static_cast<uint32_t>(input.size());
    vk::DeviceSize asTotalSize{0}; // memory size of all allocated BLAS
    uint32_t nbCompactions{0}; // Nb of BLAS requesting compaction
    vk::DeviceSize maxScratchSize{0}; // Largest scratch size

    // Preparing information for the acceleration build commands
    std::vector<BuildAccelerationStructure> buildAs(nbBlas);
    for (uint32_t idx = 0; idx < nbBlas; idx++) {
        // Filling partially the AccelerationStructureBuildGeometryInfoKHR for querying the build sizes.
        // Other info will be filled in the createBlas
        buildAs[idx].buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        buildAs[idx].buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildAs[idx].buildInfo.flags = input[idx].flags | flags;
        buildAs[idx].buildInfo.geometryCount = static_cast<uint32_t>(input[idx].geometries.size());
        buildAs[idx].buildInfo.pGeometries = input[idx].geometries.data();

        // build range info
        buildAs[idx].rangeInfo = input[idx].buildOffsetInfo.data();

        // find sizes to create acceleration structures and scratch
        std::vector<uint32_t> maxPrimCount(input[idx].buildOffsetInfo.size());
        for (auto tt = 0; tt < input[idx].buildOffsetInfo.size(); tt++) {
            maxPrimCount[tt] = input[idx].buildOffsetInfo[tt].primitiveCount; // # of traingles
        }

        buildAs[idx].sizeInfo = m_device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildAs[idx].buildInfo, maxPrimCount);

        // Extra info
        asTotalSize += buildAs[idx].sizeInfo.accelerationStructureSize;
        maxScratchSize = std::max(maxScratchSize, buildAs[idx].sizeInfo.buildScratchSize);
        nbCompactions += hasFlag(buildAs[idx].buildInfo.flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

    }
}

Acceleration createAcceleration(VulkanRenderer* VK, vk::AccelerationStructureCreateInfoKHR& accelInfo) {
    Acceleration acceleration;

    // 
}


