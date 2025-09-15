/*
* File: gpu_types
* Project: blok
* Author: Collin Longoria
* Created on: 9/12/2025
*
* Description: GPU abstraction layer
*/
#ifndef BLOK_GPU_TYPES_HPP
#define BLOK_GPU_TYPES_HPP

#include <span>
#include <string>

#include "math.hpp"
#include "gpu_flags.hpp"

/*
 * Queues and Pipeline stages
 */

enum class QueueType : uint8_t {
    GRAPHICS,
    COMPUTE,
    TRANSFER
};

// Based on Vulkan 1.4 pipeline flag bits:
// https://docs.vulkan.org/spec/latest/chapters/pipelines.html#pipelines-block-diagram
enum class PipelineStage : uint64_t {
    NONE = 0,
    TOPOFPIPELINE = 1ull << 0,
    DRAWINDIRECT = 1ull << 1,
    VERTEXINPUT = 1ull << 2,
    VERTEXSHADER = 1ull << 3,
    FRAGMENTSHADER = 1ull << 4,
    COMPUTESHADER = 1ull << 5,
    TRANSFER = 1ull << 6,
    COLORATTACHMENT = 1ull << 7,
    DEPTHSTENCIL = 1ull << 8,
    BOTTOMOFPIPELINE = 1ull << 9,
};
template<> struct is_flags_enum<PipelineStage> : std::true_type {};

/*
 * Access and Usage bits
 */

// Based on Vulkan 1.4 access flag bits
// https://registry.khronos.org/vulkan/specs/latest/man/html/VkAccessFlagBits.html
enum class Access : uint64_t {
    NONE = 0,
    INDIRECTCOMMANDREAD = 1ull << 0,
    INDEXREAD = 1ull << 1,
    VERTEXATTRIBUTEREAD = 1ull << 2,
    UNIFORMREAD = 1ull << 3,
    STORAGEREAD = 1ull << 4,
    STORAGEWRITE = 1ull << 5,
    COLORATTACHMENTREAD = 1ull << 6,
    COLORATTACHMENTWRITE = 1ull << 7,
    DEPTHSTENCILREAD = 1ull << 8,
    DEPTHSTENCILWRITE = 1ull << 9,
    TRANSFERREAD = 1ull << 10,
    TRANSFERWRITE = 1ull << 11,
    HOSTREADREAD = 1ull << 12,
    HOSTREADWRITE = 1ull << 13,
    MEMORYREAD = 1ull << 14,
    MEMORYWRITE = 1ull << 15,
};
template<> struct is_flags_enum<Access> : std::true_type {};

// Allocation helpers
enum class BufferUsage : uint64_t {
    NONE = 0,
    UPLOAD = 1ull << 0,
    READBACK = 1ull << 1,
    VERTEX = 1ull << 2,
    INDEX = 1ull << 3,
    INDIRECT = 1ull << 4,
    STORAGE = 1ull << 5,
    UNIFORM = 1ull << 6,
    COPYSOURCE = 1ull << 7,
    COPYDESTINATION = 1ull << 8,
};
template<> struct is_flags_enum<BufferUsage> : std::true_type {};

enum class ImageUsage : uint64_t {
    NONE = 0,
    SAMPLED = 1ull << 0,
    STORAGE = 1ull << 1,
    COLOR = 1ull << 2,
    DEPTH = 1ull << 3,
    COPYSOURCE = 1ull << 4,
    COPYDESTINATION = 1ull << 5,
};
template<> struct is_flags_enum<ImageUsage> : std::true_type {};

/*
 * Image and Formats
 */

enum class ImageDimensions : uint8_t {
    D1,
    D2,
    D3
};

// I asked ChatGPT to give me a list of image formats. We can add as needed.
enum class Format : uint16_t {
    UNKNOWN = 0,
    // Unsigned Normalized
    R8_UNORM,
    RG8_UNORM,
    RGBA8_UNORM,
    // Unsigned Integer
    R8_UINT,
    R16_UINT,
    R32_UINT,
    RGBA32_UINT,
    // Float
    R16_FLOAT,
    R32_FLOAT,
    RGBA16_FLOAT,
    RGBA32_FLOAT,
    // Depth/Stencil
    D24S8,
    D32_FLOAT
};

enum class IndexType : uint8_t {
    UINT16,
    UINT32
};

struct BufferDescriptor {
    size_t size = 0;
    BufferUsage usage = BufferUsage::NONE;
    bool hostVisible = false; // Upload/Readback convinience
};

struct ImageDescriptor {
    ImageDimensions dimensions = ImageDimensions::D2;
    Format format = Format::UNKNOWN;
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t mips = 1, layers = 1;
    ImageUsage usage = ImageUsage::NONE;
};

// Subresource view for SRV/UAV
struct ImageViewDescriptor {
    uint32_t baseMip = 0, mipCount = 1;
    uint32_t baseLayer = 0, layerCount = 1;
};

struct SamplerDescriptor {
    enum class Filter { NEAREST, LINEAR };
    enum class Address { REPEAT, CLAMP, MIRROR };

    Filter minFilter = Filter::LINEAR;
    Filter magFilter = Filter::LINEAR;
    Filter mipFilter = Filter::LINEAR;

    Address addressU = Address::REPEAT;
    Address addressV = Address::REPEAT;
    Address addressW = Address::REPEAT;

    float mipLoadBias = 0.0f;
    float minLod = 0.0f, maxLod = 1000.0f;
    float maxAnisotropy = 1.0f;
    bool compareEnable = false;
};

/*
 * Binding and Layouts
 */

enum class BindingType : uint8_t {
    UNIFORMBUFFER,
    STORAGEBUFFER,
    SAMPLEDIMAGE,
    STORAGEIMAGE,
    SAMPLER
};

struct BindGroupLayoutEntry {
    uint32_t binding = 0;
    BindingType type = BindingType::UNIFORMBUFFER;
    uint32_t count = 1;
    PipelineStage visibleStages = PipelineStage::VERTEXSHADER | PipelineStage::FRAGMENTSHADER |
        PipelineStage::COMPUTESHADER;
};

struct BindGroupLayoutDescriptor {
    std::vector<BindGroupLayoutEntry> entries;
};

struct PushConstantRange {
    PipelineStage stage = PipelineStage::COMPUTESHADER;
    uint32_t offset = 0; // bytes
    uint32_t size = 0; // also in bytes
};

struct PipelineLayoutDescriptor {
    std::vector<uint64_t> setLayouts; // BindGroupLayout handles
    std::vector<PushConstantRange> pushConstants;
};

// Shader modules
// Note this will probably be rewritten when we complete a good shader pipeline
// Since the APIs both only support one type anyways
// This is from my vulkan work, hence why its SPIR-v biased

// No intention of DirectX so I am not putting HLSL here
enum class ShaderIR : uint8_t {
    UNKNOWN,
    SPIRV,
    GLSL,
    WGSL
};

// For similar reasons, no Geo shader or others. Feel free to implement all these later
enum class ShaderStage : uint8_t {
    VERTEX,
    FRAGMENT,
    COMPUTE
};

struct ShaderDefine {
    std::string name;
    std::string value;
};

struct ShaderModuleDescriptor {
    ShaderIR ir = ShaderIR::SPIRV;
    ShaderStage stage = ShaderStage::COMPUTE;
    std::span<const uint8_t> bytes;
    std::string entryPoint = "main";
    std::vector<ShaderDefine> defines;
};

// Pipelines
struct VertexAttributeDescriptor {
    uint32_t location = 0, binding = 0, offset = 0, stride = 0;
    Format format = Format::RGBA32_FLOAT;
};

// I highly doubt we will use all of these, but I included them anyways
// Just grabbed the ones from here:
// https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-primitive-topologies
enum class PrimitiveTopology : uint8_t {
    TRIANGLELIST,
    TRIANGLESTRIP,
    LINELIST,
    POINTLIST
};

// Similarly, these next few will generally go unused to a full extent
// but to be clear, we are making it so nothing would be hardcoded during device or pipeline creation
enum class CullMode : uint8_t {
    NONE,
    FRONT,
    BACK
};

enum class FrontFace : uint8_t {
    CCW,
    CW
};

// TODO: finish this
struct DepthState {
    bool depthTest = true;
    bool depthWrite = true;
    bool depthClamp = false;
};

// TODO: finish this
struct BlendState {
    bool enable = false;
};

struct GraphicsPipelineDescriptor {
    uint64_t vs = 0, fs = 0; // ShaderModule handles
    uint64_t pipelineLayout = 0; // PipelineLayout handle

    std::vector<VertexAttributeDescriptor> vertexInputs;
    PrimitiveTopology primitiveTopology = PrimitiveTopology::TRIANGLELIST;

    CullMode cull = CullMode::BACK;
    FrontFace frontFace = FrontFace::CCW;

    DepthState depth;
    BlendState blend;

    Format colorFormat = Format::RGBA8_UNORM;
    Format depthFormat = Format::D24S8;
};

struct ComputePipelineDescriptor {
    uint64_t cs = 0; // ShaderModule handle
    uint64_t pipelineLayout = 0; // PipelineLayout handle
};

/*
 * Device Capabiltiies
 */

// again, very vulkan oriented, but it should work for everything
struct DeviceCapabilities {
    uint32_t uniformBufferAlignment = 256;
    uint32_t storageBufferAlignment = 256;
    uint32_t maxPushConstantBytes = 128;
    std::array<uint32_t, 3> maxComputeWorkgroupSize {1024,1024,64};
    uint32_t maxComputeSharedMemoryBytes = 32 * 1024;
    bool hasTimelineSemaphore = false;
    bool hasExternalMemoryInterop = false;
    // TODO: Add more extensions as needed...
};
#endif //BLOK_GPU_TYPES_HPP