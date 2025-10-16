/*
* File: descriptor_system
* Project: blok
* Author: Collin Longoria
* Created on: 10/16/2025
*
* Description:
*/
#ifndef BLOK_DESCRIPTOR_SYSTEM_HPP
#define BLOK_DESCRIPTOR_SYSTEM_HPP

#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace blok {

struct BindingKey {
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
    VkShaderStageFlags stages;
    bool operator==(const BindingKey& o) const noexcept {
        return binding == o.binding && type == o.type && count == o.count && stages == o.stages;
    }
};

struct SetLayoutKey {
    std::vector<BindingKey> bindings;
    bool operator==(const SetLayoutKey& o) const noexcept { return bindings == o.bindings; }
};

struct SetLayoutKeyHash {
    size_t operator()(const SetLayoutKey& k) const noexcept {
        size_t h = 0;
        for (auto& b : k.bindings)
            h ^= (b.binding << 1) ^ static_cast<size_t>(b.type) ^ (b.count << 3) ^ static_cast<size_t>(b.stages);
        return h;
    }
};

class DescriptorSetLayoutCache {
public:
    explicit DescriptorSetLayoutCache(vk::Device d) : m_device(d) {}
    ~DescriptorSetLayoutCache() { destroyAll(); }

    vk::DescriptorSetLayout get(const SetLayoutKey& key);
    void destroyAll();

private:
    vk::Device m_device{};
    std::unordered_map<SetLayoutKey, vk::DescriptorSetLayout, SetLayoutKeyHash> m_cache;
};

class DescriptorAllocator {
public:
    explicit DescriptorAllocator(vk::Device d) : m_device(d) {}
    ~DescriptorAllocator() { reset(); }

    vk::DescriptorSet allocate(vk::DescriptorSetLayout layout);
    void reset();

private:
    vk::Device m_device{};
    std::vector<vk::DescriptorPool> m_usedPools;
    std::vector<vk::DescriptorPool> m_freePools;
    vk::DescriptorPool currentPool{};

    vk::DescriptorPool getPool();
    vk::DescriptorPool createPool(uint32_t count = 128);
};

}

#endif //BLOK_DESCRIPTOR_SYSTEM_HPP