/*
* File: descriptors.hpp
* Project: blok
* Author: Collin Longoria
* Created on: 12/2/2025
*/
#ifndef DESCRIPTORS_HPP
#define DESCRIPTORS_HPP
#include <deque>
#include <unordered_map>
#include <span>
#include "vulkan_context.hpp"

namespace blok {

struct DescriptorWriter {
    std::deque<vk::DescriptorImageInfo> imageInfos;
    std::deque<vk::DescriptorBufferInfo> bufferInfos;
    std::vector<vk::WriteDescriptorSet> writes;

    void write_image(int binding, vk::ImageView image, vk::Sampler sampler, vk::ImageLayout layout, vk::DescriptorType type);
    void write_buffer(int binding, vk::Buffer buffer, size_t size, size_t offset, vk::DescriptorType type);

    void clear();
    void updateSet(vk::Device device, vk::DescriptorSet set);
};

struct DescriptorAllocatorGrowable {
public:
    struct PoolSizeRatio {
        vk::DescriptorType type;
        float ratio;
    };

    void init(vk::Device device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clearPools(vk::Device device);
    void destroyPools(vk::Device device);

    vk::DescriptorSet allocate(vk::Device device, vk::DescriptorSetLayout layout, void* pNext = nullptr);

private:
    vk::DescriptorPool getPool(vk::Device device);
    vk::DescriptorPool createPool(vk::Device device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

    std::vector<PoolSizeRatio> m_ratios;
    std::vector<vk::DescriptorPool> m_fullPools;
    std::vector<vk::DescriptorPool> m_readyPools;
    uint32_t setsPerPool;
};

}

#endif //DESCRIPTORS_HPP