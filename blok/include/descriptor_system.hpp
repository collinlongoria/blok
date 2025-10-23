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

struct DSLBindingDesc {
    uint32_t binding = 0;
    vk::DescriptorType type{};
    vk::ShaderStageFlagBits stages{};
    uint32_t count = 1; // array length
};

struct DescriptorSetLayoutDesc {
    std::string name; // ID referenced in YAML
    std::vector<DSLBindingDesc> bindings;
};

struct DescriptorPoolSizes {
    // Just doing a large pre allocate. Someone have a better idea? Let me know.
    std::vector<vk::DescriptorPoolSize> sizes;
    uint32_t maxSets = 1024;
};

struct DescriptorSetLayouts {
    std::vector<vk::UniqueDescriptorSetLayout> layouts; // in set-index order
};

class DescriptorSystem {
public:
    void init(vk::Device device, const DescriptorPoolSizes& poolCfg);
    void shutdown();

    DescriptorSetLayouts createLayouts(const std::vector<DescriptorSetLayoutDesc>& descs);

    std::vector<vk::DescriptorSet> allocateSets(const std::vector<vk::DescriptorSetLayout>& layouts, uint32_t count=1);

    vk::DescriptorPool pool() const { return m_pool.get(); }

private:
    vk::Device m_device;
    vk::UniqueDescriptorPool m_pool;
};

}

#endif //BLOK_DESCRIPTOR_SYSTEM_HPP