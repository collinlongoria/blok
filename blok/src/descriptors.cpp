#include "descriptors.hpp"

namespace blok {

void DescriptorAllocatorGrowable::init(vk::Device device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
    m_ratios.clear();

    for (auto r : poolRatios) {
        m_ratios.push_back(r);
    }

    vk::DescriptorPool nPool = createPool(device, maxSets, poolRatios);

    setsPerPool = maxSets * 1.5; // grow it next allocation

    m_readyPools.push_back(nPool);
}

void DescriptorAllocatorGrowable::clearPools(vk::Device device) {
    for (auto p : m_readyPools)
        device.resetDescriptorPool(p);
    for (auto p : m_fullPools) {
        device.resetDescriptorPool(p);
        m_readyPools.push_back(p);
    }
    m_fullPools.clear();
}

void DescriptorAllocatorGrowable::destroyPools(vk::Device device) {
    for (auto p : m_readyPools)
        device.destroyDescriptorPool(p);
    m_readyPools.clear();
    for (auto p : m_fullPools)
        device.destroyDescriptorPool(p);
    m_fullPools.clear();
}

vk::DescriptorPool DescriptorAllocatorGrowable::getPool(vk::Device device) {
    vk::DescriptorPool nPool;
    if (m_readyPools.size() != 0) {
        nPool = m_readyPools.back();
        m_readyPools.pop_back();
    }
    else {
        // create new pool
        nPool = createPool(device, setsPerPool, m_ratios);

        setsPerPool = static_cast<uint32_t>(setsPerPool * 1.5);
        if (setsPerPool > 4092)
            setsPerPool = 4092; // this is the MAX limit. can change.
    }

    return nPool;
}

vk::DescriptorPool DescriptorAllocatorGrowable::createPool(vk::Device device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios) {
    std::vector<vk::DescriptorPoolSize> poolSizes;
    for (PoolSizeRatio ratio : poolRatios) {
        vk::DescriptorPoolSize curr{};
        curr.type = ratio.type;
        curr.descriptorCount = static_cast<uint32_t>(ratio.ratio * setCount);

        poolSizes.push_back(curr);
    }

    vk::DescriptorPoolCreateInfo ci{};
    ci.maxSets = setCount;
    ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    ci.pPoolSizes = poolSizes.data();

    vk::DescriptorPool nPool;
    if (auto result = device.createDescriptorPool(&ci, nullptr, &nPool) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create descriptor pool");
    }
    return nPool;
}

vk::DescriptorSet DescriptorAllocatorGrowable::allocate(vk::Device device, vk::DescriptorSetLayout layout, void *pNext) {
    // get or create a pool to allocate from
    vk::DescriptorPool poolToUse = getPool(device);

    vk::DescriptorSetAllocateInfo ai{};
    ai.pNext = pNext;
    ai.descriptorPool = poolToUse;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;

    vk::DescriptorSet ds;
    auto result = device.allocateDescriptorSets(&ai, &ds);

    // allocation failed - pool might have been full. try one more time
    if (result == vk::Result::eErrorOutOfPoolMemory || result == vk::Result::eErrorFragmentedPool) {
        m_fullPools.push_back(poolToUse);

        poolToUse = getPool(device);
        ai.descriptorPool = poolToUse;

        if (auto result2 = device.allocateDescriptorSets(&ai, &ds) != vk::Result::eSuccess) {
            throw std::runtime_error("failed to allocate descriptor sets");
        }
    }

    m_readyPools.push_back(poolToUse);
    return ds;
}

void DescriptorWriter::write_buffer(int binding, vk::Buffer buffer, size_t size, size_t offset, vk::DescriptorType type) {

    vk::DescriptorBufferInfo& info = bufferInfos.emplace_back();
    info.buffer = buffer;
    info.offset = offset;
    info.range = size;

    vk::WriteDescriptorSet write{};

    write.dstBinding = binding;
    write.dstSet = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &info;

    writes.push_back(write);
}

void DescriptorWriter::write_image(int binding, vk::ImageView image, vk::Sampler sampler, vk::ImageLayout layout, vk::DescriptorType type) {

    vk::DescriptorImageInfo& info = imageInfos.emplace_back();
    info.sampler = sampler;
    info.imageView = image;
    info.imageLayout = layout;

    vk::WriteDescriptorSet write{};

    write.dstBinding = binding;
    write.dstSet = VK_NULL_HANDLE;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = &info;

    writes.push_back(write);
}

void DescriptorWriter::clear() {
    imageInfos.clear();
    writes.clear();
    bufferInfos.clear();
}

void DescriptorWriter::updateSet(vk::Device device, vk::DescriptorSet set) {
    for (vk::WriteDescriptorSet& write : writes) {
        write.dstSet = set;
    }

    device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

}