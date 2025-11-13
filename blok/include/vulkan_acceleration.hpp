/*
* File: vulkan_acceleration
* Project: blok
* Author: Collin Longoria
* Created on: 11/12/2025
*
* Description:
*/
#ifndef BLOK_VULKAN_ACCELERATION_HPP
#define BLOK_VULKAN_ACCELERATION_HPP
#include <vulkan/vulkan.hpp>

#include "math.hpp"
#include "vulkan_renderer.hpp"

namespace blok {

// column-major -> row-major
inline vk::TransformMatrixKHR toTransformMatrixKHR(Matrix4 matrix) {
    Matrix4 temp = glm::transpose(matrix);
    vk::TransformMatrixKHR out_matrix;
    memcpy(&out_matrix, &temp, sizeof(vk::TransformMatrixKHR));
    return out_matrix;
}

struct Acceleration {
    vk::AccelerationStructureKHR acceleration;
    Buffer buffer;
};

struct BlasInput {
    // Data used to build acceleration structure geometry
    std::vector<vk::AccelerationStructureGeometryKHR> geometries;
    std::vector<vk::AccelerationStructureBuildRangeInfoKHR> buildOffsetInfo;
    vk::BuildAccelerationStructureFlagBitsKHR flags{0};
};

// Raytracing BLAS and TLAS builder
class RaytracingBuilder {
public:
    VulkanRenderer* VK;

    // Init the allocator and query the raytracing props
    void init(VulkanRenderer* vk, const vk::Device& device, uint32_t queueIndex);

    // Destroy all allocations
    void destroy();

    // Return the constructed top-level acceleration structure
    vk::AccelerationStructureKHR getAccelerationStructure() const;

    // Return the Acceleration Structure Device Address of a BLAS Id
    vk::DeviceAddress getBlasDeviceAddress(uint32_t blasId);

    // Create all the BLAS from the vector of BlasInput
    void buildBlas(const std::vector<BlasInput>& input, vk::BuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    // Refit BLAS number blasIdx from updated buffer contents
    void updateBlas(uint32_t blasIdx, BlasInput& blas, vk::BuildAccelerationStructureFlagsKHR flags);

    // Build TLAS from an array of AccelerationStructureInstance
    void buildTlas(const std::vector<vk::AccelerationStructureInstanceKHR>& instances, vk::BuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, bool update = false, bool motion = false);

    // Create the TLAS, called by buildTlas
    void cmdCreateTlas(vk::CommandBuffer cmdBuf, uint32_t countInstance, vk::DeviceAddress instBufferAddr, vk::BuildAccelerationStructureFlagsKHR flags, bool update, bool motion);

protected:
    std::vector<Acceleration> m_blas; // bottom-level acceleration structure
    Acceleration m_tlas; // top-level acceleration structure

    // Setup
    vk::Device m_device{VK_NULL_HANDLE};
    uint32_t m_queueIndex{0};

    struct BuildAccelerationStructure {
        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        const vk::AccelerationStructureBuildRangeInfoKHR* rangeInfo;
        Acceleration as; // result acceleration structure
        vk::AccelerationStructureKHR cleanupAS;
    };

    void cmdCreateBlas(vk::CommandBuffer cmdBuf, std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs, vk::DeviceAddress scratchAddress, vk::QueryPool queryPool);
    void cmdCompactBlas(vk::CommandBuffer cmdBuf, std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs, vk::QueryPool queryPool);
    void destroyNonCompacted(std::vector<uint32_t> indices, std::vector<BuildAccelerationStructure>& buildAs);
    bool hasFlag(VkFlags item, VkFlags flag) { return (item & flag) == flag; }
};

}

#endif //BLOK_VULKAN_ACCELERATION_HPP
