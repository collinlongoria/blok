
// ----------------------------------------------------------------------------
// Renderer_WebGPU.cpp
#include "webgpu_renderer.hpp"
#include <chrono>
#include <cstring>
#include <stdexcept>
#include "shader_pipe.hpp"

using namespace blok;

static const char* kComputeGLSL = R"GLSL(
#version 450
layout(local_size_x=8, local_size_y=8) in;

// storage image the ray tracer writes to
layout(binding=0, rgba8) uniform writeonly image2D outImage;

layout(std140, binding=1) uniform Params { float t; int width; int height; float pad; } pc;

float saturate(float x){ return clamp(x,0.0,1.0);} 
vec3 sky(vec2 uv){
    float y = uv.y;
    return mix(vec3(1.0), vec3(0.5,0.7,1.0), 0.5*(y+1.0));
}

void main(){
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= pc.width || gid.y >= pc.height) return;

    // Simple animated gradient to prove plumbing; replace with full RT if desired
    vec2 uv = vec2((float(gid.x)+0.5)/pc.width, (float(gid.y)+0.5)/pc.height);
    float a = 0.5 + 0.5*cos(pc.t + uv.x*6.283);
    float b = 0.5 + 0.5*sin(pc.t*1.3 + uv.y*6.283);
    vec3 col = mix(vec3(a, b, 1.0-a), sky(uv*2.0-1.0), 0.25);

    // optional simple gamma
    //col = pow(clamp(col,0.0,1.0), vec3(1.0/2.2));
    imageStore(outImage, gid, vec4(col, 1.0));
}
)GLSL";

static const char* kBlitVS = R"GLSL(
#version 450
layout(location=0) out vec2 vUV;
vec2 verts[3] = vec2[3]( vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0) );
void main(){
    gl_Position = vec4(verts[gl_VertexIndex], 0.0, 1.0);
    vUV = 0.5*gl_Position.xy + 0.5;
}
)GLSL";

static const char* kBlitFS = R"GLSL(
#version 450
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 frag;
layout(binding=0) uniform sampler samp;
layout(binding=1) uniform texture2D texImg;
void main(){ frag = texture(sampler2D(texImg, samp), vUV); }
)GLSL";

RendererWebGPU::RendererWebGPU(std::shared_ptr<Window> window)
    : m_window(std::move(window)) {}
RendererWebGPU::~RendererWebGPU(){ if (m_gpu) shutdown(); }

void RendererWebGPU::init(){
    DeviceInitInfo init{};
    init.width = m_window->getWidth();
    init.height = m_window->getHeight();
    init.presentMode = PresentMode::MAILBOX;
    init.backend = RenderBackend::WEBGPU_VULKAN;
    init.windowHandle = m_window.get();
    m_gpu = std::make_unique<WebGPUDevice>(init);

    m_width = init.width; m_height = init.height;
    createSwapchain();
    createRayTarget();
    createSamplers();

    // UBO
    BufferDescriptor ub{}; ub.size = sizeof(Uniforms); ub.usage = BufferUsage::UNIFORM | BufferUsage::COPYDESTINATION; 
    m_ubo = m_gpu->createBuffer(ub, nullptr);

    createComputePipeline();
    createBlitPipeline();
}

void RendererWebGPU::createSwapchain(){
    SwapchainDescriptor sd{}; sd.width = m_width; sd.height = m_height; sd.format = m_gpu->backbufferFormat(); sd.presentMode = PresentMode::MAILBOX;
    m_swap = m_gpu->createSwapchain(sd);
}

void RendererWebGPU::createRayTarget(){
    ImageDescriptor id{}; id.dimensions = ImageDimensions::D2; id.format = Format::RGBA8_UNORM; id.width = m_width; id.height = m_height; id.usage = ImageUsage::STORAGE | ImageUsage::SAMPLED | ImageUsage::COPYDESTINATION | ImageUsage::COPYSOURCE;
    m_rayImage = m_gpu->createImage(id, nullptr);

    ImageViewDescriptor vd{}; vd.baseMip = 0; vd.mipCount = 1; vd.baseLayer = 0; vd.layerCount = 1;
    m_rayView = m_gpu->createImageView(m_rayImage, vd);
}

void RendererWebGPU::createSamplers(){
    SamplerDescriptor sd{}; sd.minFilter = SamplerDescriptor::Filter::LINEAR; sd.magFilter = SamplerDescriptor::Filter::LINEAR; sd.mipFilter = SamplerDescriptor::MipFilter::NEAREST;
    m_sampler = m_gpu->createSampler(sd);
}

void RendererWebGPU::createComputePipeline(){
    // Bind group layout: 0 = storage image, 1 = Params UBO
    BindGroupLayoutDescriptor bgl{};
    bgl.entries = {
        BindGroupLayoutEntry{ .binding = 0, .type = BindingType::STORAGEIMAGE,  .visibleStages = PipelineStage::COMPUTESHADER },
        BindGroupLayoutEntry{ .binding = 1, .type = BindingType::UNIFORMBUFFER, .visibleStages = PipelineStage::COMPUTESHADER },
    };
    m_cbgl = m_gpu->createBindGroupLayout(bgl);

    PipelineLayoutDescriptor pl{}; pl.setLayouts = { m_cbgl };
    m_cpl = m_gpu->createPipelineLayout(pl);

    // Compile GLSL -> SPIR-V
    auto csSPV = shaderpipe::glsl_to_spirv(kComputeGLSL, shaderpipe::ShaderStage::COMPUTE);
    ShaderModuleDescriptor smd{}; smd.ir = ShaderIR::SPIRV; smd.stage = ShaderStage::COMPUTE; smd.bytes = std::span<const uint8_t>((const uint8_t*)csSPV.data(), csSPV.size()*sizeof(uint32_t)); smd.entryPoint = "main";
    m_cs = m_gpu->createShaderModule(smd);

    ComputePipelineDescriptor cpd{}; cpd.cs = m_cs; cpd.pipelineLayout = m_cpl;
    m_cpipe = m_gpu->createComputePipeline(cpd);

    // Create bind group
    IGPUDevice::BindGroupDescriptor bg{}; bg.layout = m_cbgl; bg.entries = {
        // binding 0: storage image view
        IGPUDevice::BindGroupEntry{ .binding=0, .kind=IGPUDevice::BindGroupEntry::Kind::IMAGEVIEW, .handle=(uint64_t)m_rayView },
        IGPUDevice::BindGroupEntry{ .binding=1, .kind=IGPUDevice::BindGroupEntry::Kind::BUFFER, .handle=(uint64_t)m_ubo, .offset=0, .size=sizeof(Uniforms) },
    };
    m_cbg = m_gpu->createBindGroup(bg);
}

void RendererWebGPU::createBlitPipeline(){
    // Bind group layout: sampler + sampled texture
    BindGroupLayoutDescriptor bgl{}; bgl.entries = {
        BindGroupLayoutEntry{ .binding = 0, .type = BindingType::SAMPLER,      .visibleStages = PipelineStage::FRAGMENTSHADER },
        BindGroupLayoutEntry{ .binding = 1, .type = BindingType::SAMPLEDIMAGE, .visibleStages = PipelineStage::FRAGMENTSHADER },
    }; m_gbgl = m_gpu->createBindGroupLayout(bgl);

    PipelineLayoutDescriptor pl{}; pl.setLayouts = { m_gbgl }; m_gpl = m_gpu->createPipelineLayout(pl);

    // GLSL -> SPIR-V
    auto vsSPV = shaderpipe::glsl_to_spirv(kBlitVS, shaderpipe::ShaderStage::VERTEX);
    auto fsSPV = shaderpipe::glsl_to_spirv(kBlitFS, shaderpipe::ShaderStage::FRAGMENT);

    ShaderModuleDescriptor vsm{}; vsm.ir=ShaderIR::SPIRV; vsm.stage=ShaderStage::VERTEX;   vsm.bytes=std::span<const uint8_t>((const uint8_t*)vsSPV.data(), vsSPV.size()*sizeof(uint32_t)); vsm.entryPoint="main";
    ShaderModuleDescriptor fsm{}; fsm.ir=ShaderIR::SPIRV; fsm.stage=ShaderStage::FRAGMENT; fsm.bytes=std::span<const uint8_t>((const uint8_t*)fsSPV.data(), fsSPV.size()*sizeof(uint32_t)); fsm.entryPoint="main";
    m_vs = m_gpu->createShaderModule(vsm);
    m_fs = m_gpu->createShaderModule(fsm);

    GraphicsPipelineDescriptor gpd{};
    gpd.vs = m_vs; gpd.fs = m_fs; gpd.pipelineLayout = m_gpl;
    gpd.primitiveTopology = PrimitiveTopology::TRIANGLELIST;
    gpd.frontFace = FrontFace::CCW; gpd.cull = CullMode::NONE;
    gpd.depth = { .depthTest = false, .depthWrite = false };
    gpd.depthFormat = Format::UNKNOWN;
    gpd.colorFormat = m_gpu->backbufferFormat();
    gpd.blend = { .enable = false };
    gpd.vertexInputs = {}; // full-screen triangle uses gl_VertexIndex

    m_gpipe = m_gpu->createGraphicsPipeline(gpd);

    // graphics bind group
    IGPUDevice::BindGroupDescriptor bg{}; bg.layout = m_gbgl; bg.entries = {
        IGPUDevice::BindGroupEntry{ .binding=0, .kind=IGPUDevice::BindGroupEntry::Kind::SAMPLER, .handle=(uint64_t)m_sampler },
        IGPUDevice::BindGroupEntry{ .binding=1, .kind=IGPUDevice::BindGroupEntry::Kind::IMAGEVIEW,   .handle=(uint64_t)m_rayView },
    }; m_gbg = m_gpu->createBindGroup(bg);
}

void RendererWebGPU::beginFrame(){ /* nothing: ImGui not wired for WebGPU here */ }

void RendererWebGPU::drawFrame(const Camera& /*cam*/, const Scene& /*scene*/){
    // Update uniforms (time)
    static auto t0 = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    float t = std::chrono::duration<float>(now - t0).count();
    Uniforms u{ t, (int)m_width, (int)m_height, 0.0f };
    m_gpu->updateBuffer(m_ubo, 0, sizeof(Uniforms), &u);

    // Acquire backbuffer
    ImageViewHandle bbView = m_gpu->acquireNextImage(m_swap);
    if (!bbView) return;

    // Compute pass
    ICommandList* clc = m_gpu->createCommandList(QueueType::COMPUTE);
    clc->begin();
    clc->bindComputePipeline(m_cpipe);
    clc->bindBindGroup(0, m_cbg);
    const uint32_t gx = (m_width  + 7) / 8;
    const uint32_t gy = (m_height + 7) / 8;
    clc->dispatch(gx, gy, 1);
    clc->end();

    // Render blit pass
    ICommandList* clr = m_gpu->createCommandList(QueueType::GRAPHICS);
    clr->begin();
    AttachmentDescriptor color{}; color.view = bbView; color.load = AttachmentDescriptor::LoadOperation::CLEAR; color.store = AttachmentDescriptor::StoreOperation::STORE; color.clearColor = {0.05f,0.05f,0.08f,1.0f};
    RenderPassBeginInfo rp{}; rp.colorAttachments = { color };
    clr->beginRenderPass(rp);
    clr->bindGraphicsPipeline(m_gpipe);
    clr->bindBindGroup(0, m_gbg);
    clr->draw(3, 1, 0, 0); // full-screen triangle
    clr->endRenderPass();
    clr->end();

    // Submit
    std::vector<ICommandList*> commands = std::vector<ICommandList*>{clc, clr};
    SubmitBatch batch{}; batch.lists = std::span<ICommandList*>( commands ); m_gpu->submit(batch);
    m_gpu->present(m_swap);

    m_gpu->destroyCommandList(clc);
    m_gpu->destroyCommandList(clr);
}

void RendererWebGPU::endFrame(){ /* nothing */ }

void RendererWebGPU::shutdown(){
    if (!m_gpu) return;

    if (m_gbg) m_gpu->destroyBindGroup(m_gbg);
    if (m_gbgl) m_gpu->destroyBindGroupLayout(m_gbgl);
    if (m_gpipe) m_gpu->destroyGraphicsPipeline(m_gpipe);
    if (m_vs) m_gpu->destroyShaderModule(m_vs);
    if (m_fs) m_gpu->destroyShaderModule(m_fs);

    if (m_cbg) m_gpu->destroyBindGroup(m_cbg);
    if (m_cbgl) m_gpu->destroyBindGroupLayout(m_cbgl);
    if (m_cpipe) m_gpu->destroyComputePipeline(m_cpipe);
    if (m_cs) m_gpu->destroyShaderModule(m_cs);

    if (m_sampler) m_gpu->destroySampler(m_sampler);
    if (m_rayView) m_gpu->destroyImageView(m_rayView);
    if (m_rayImage) m_gpu->destroyImage(m_rayImage);
    if (m_ubo) m_gpu->destroyBuffer(m_ubo);

    if (m_swap) m_gpu->destroySwapchain(m_swap);

    //m_gpu->waitIdle(QueueType::GRAPHICS);
    //m_gpu->waitIdle(QueueType::COMPUTE);
    m_gpu.reset();
}

void RendererWebGPU::resize(uint32_t w, uint32_t h){
    if (w==m_width && h==m_height) return;
    m_width = w; m_height = h;
    // Recreate swapchain + ray target
    if (m_swap){ m_gpu->destroySwapchain(m_swap); m_swap = {}; }
    if (m_rayView){ m_gpu->destroyImageView(m_rayView); m_rayView = {}; }
    if (m_rayImage){ m_gpu->destroyImage(m_rayImage); m_rayImage = {}; }
    createSwapchain();
    createRayTarget();

    // Recreate compute bind group (points at the new m_rayView)
    if (m_cbg) { m_gpu->destroyBindGroup(m_cbg); m_cbg = {}; }
    IGPUDevice::BindGroupDescriptor bg{}; bg.layout = m_cbgl; bg.entries = {
        IGPUDevice::BindGroupEntry{ .binding=0, .kind=IGPUDevice::BindGroupEntry::Kind::IMAGEVIEW, .handle=(uint64_t)m_rayView },
        IGPUDevice::BindGroupEntry{ .binding=1, .kind=IGPUDevice::BindGroupEntry::Kind::BUFFER, .handle=(uint64_t)m_ubo, .offset=0, .size=sizeof(Uniforms) },
    }; m_cbg = m_gpu->createBindGroup(bg);

    // Recreate graphics bind group (sample new view)
    if (m_gbg) { m_gpu->destroyBindGroup(m_gbg); m_gbg = {}; }
    IGPUDevice::BindGroupDescriptor gbg{}; gbg.layout = m_gbgl; gbg.entries = {
        IGPUDevice::BindGroupEntry{ .binding=0, .kind=IGPUDevice::BindGroupEntry::Kind::SAMPLER, .handle=(uint64_t)m_sampler },
        IGPUDevice::BindGroupEntry{ .binding=1, .kind=IGPUDevice::BindGroupEntry::Kind::IMAGEVIEW,   .handle=(uint64_t)m_rayView },
    }; m_gbg = m_gpu->createBindGroup(gbg);
}
