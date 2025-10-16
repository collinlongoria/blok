/*
* File: descriptor_system
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/

#include "descriptor_system.hpp"

using namespace blok;

vk::DescriptorSetLayout DescriptorSetLayoutCache::get(const SetLayoutKey &key) {
    if (auto it = m_cache.find(key); it != m_cache.end()) return it->second;

    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.reserve(key.bindings.size());
    for (auto& b : key.bindings)
        bindings.push_back({
            b.binding,
            static_cast<vk::DescriptorType>(b.type),
            b.count,
            static_cast<vk::ShaderStageFlags>(b.stages)});

    vk::DescriptorSetLayoutCreateInfo info{};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();

    auto layout = m_device.createDescriptorSetLayout(info);
    m_cache.emplace(key, layout);
    return layout;
}

void DescriptorSetLayoutCache::destroyAll() {
    for (auto& [_, l] : m_cache)
        m_device.destroyDescriptorSetLayout(l);
    m_cache.clear();
}

vk::DescriptorSet DescriptorAllocator::allocate(vk::DescriptorSetLayout layout) {
    if (!currentPool) currentPool = getPool();

    vk::DescriptorSetAllocateInfo ai{currentPool, 1, &layout};
    auto sets = m_device.allocateDescriptorSets(ai);
    if (sets.empty()) {
        currentPool = getPool();
        sets = m_device.allocateDescriptorSets(ai);
    }
    return sets.front();
}

void DescriptorAllocator::reset() {
    for (auto& pool : m_usedPools) m_device.destroyDescriptorPool(pool);
    for (auto& pool : m_freePools) m_device.destroyDescriptorPool(pool);
    m_usedPools.clear();
    m_freePools.clear();
    currentPool = nullptr;
}

vk::DescriptorPool DescriptorAllocator::getPool() {
    if (!m_freePools.empty()) {
        auto p = m_freePools.back();
        m_freePools.pop_back();
        m_usedPools.push_back(p);
        return p;
    }
    auto p = createPool(128);
    m_usedPools.push_back(p);
    return p;
}

vk::DescriptorPool DescriptorAllocator::createPool(uint32_t count) {
    std::vector<vk::DescriptorPoolSize> sizes = {
        {vk::DescriptorType::eUniformBuffer, count * 4},
        {vk::DescriptorType::eCombinedImageSampler, count * 4},
        {vk::DescriptorType::eStorageImage, count * 2},
        {vk::DescriptorType::eStorageBuffer, count * 2}
    };
    vk::DescriptorPoolCreateInfo info{};
    info.maxSets = count;
    info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    return m_device.createDescriptorPool(info);
}
