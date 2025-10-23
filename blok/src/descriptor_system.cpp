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

void DescriptorSystem::init(vk::Device device, const DescriptorPoolSizes &poolCfg) {
    m_device = device;
    vk::DescriptorPoolCreateInfo pci{};
    pci.setMaxSets(poolCfg.maxSets).setPoolSizes(poolCfg.sizes);
    m_pool = m_device.createDescriptorPoolUnique(pci);
}

void DescriptorSystem::shutdown() {
    m_pool.reset();
}

DescriptorSetLayouts DescriptorSystem::createLayouts(const std::vector<DescriptorSetLayoutDesc> &descs) {
    DescriptorSetLayouts out{};
    out.layouts.reserve(descs.size());
    for (const auto& d : descs) {
        std::vector<vk::DescriptorSetLayoutBinding> bs;
        bs.reserve(d.bindings.size());
        for (const auto& b : d.bindings) {
            bs.push_back({b.binding, b.type, b.count, b.stages});
        }
        vk::DescriptorSetLayoutCreateInfo ci{}; ci.setBindings(bs);
        out.layouts.push_back(m_device.createDescriptorSetLayoutUnique(ci));
    }
    return out;
}

std::vector<vk::DescriptorSet> DescriptorSystem::allocateSets(const std::vector<vk::DescriptorSetLayout> &layouts, uint32_t count) {
    std::vector<vk::DescriptorSetLayout> ls; ls.reserve(layouts.size() * count);
    for (uint32_t i = 0; i < count; ++i) for (auto l: layouts) ls.push_back(l);
    vk::DescriptorSetAllocateInfo ai{}; ai.setDescriptorPool(m_pool.get()).setSetLayouts(ls);
    auto sets = m_device.allocateDescriptorSets(ai);
    return sets;
}
