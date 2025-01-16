#include "Application.h"

#include <vector>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "WebGPUUtils.h"
#include "sdl2webgpu.h"

// We embbed the source of the shader module here
const char* shaderSource = R"(

struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec3f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let ratio = 1024.0 / 768.0;
    out.position = vec4f(in.position.x, in.position.y * ratio, 0.0, 1.0);
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return vec4f(in.color, 1.0);
}

)";

bool Application::Initialize()
{
    // Init SDL
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Dawn Sample",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              1024,
                              768,
                              0);

    // Create instance
#ifdef __EMSCRIPTEN__
    wgpu::Instance instance = wgpu::CreateInstance(nullptr);
#else
    wgpu::InstanceDescriptor desc = {};
    desc.nextInChain              = nullptr;

    wgpu::DawnTogglesDescriptor toggles;
    toggles.nextInChain         = nullptr;
    toggles.sType               = wgpu::SType::DawnTogglesDescriptor;
    toggles.disabledToggleCount = 0;
    toggles.enabledToggleCount  = 1;
    const char* toggleName      = "enable::immediate::error::handling";
    toggles.enabledToggles      = &toggleName;

    desc.nextInChain = &toggles;

    wgpu::Instance instance = wgpu::CreateInstance(&desc);
#endif
    if (instance == nullptr)
    {
        SDL_Log("Instance creation failed!");
        return false;
    }

    // Create WebGPU surface
    surface = SDL_GetWGPUSurface(instance, window);

    // Requesting Adapter
    wgpu::RequestAdapterOptions adapterOptions = {};
    adapterOptions.nextInChain                 = nullptr;
    adapterOptions.compatibleSurface           = surface;
    wgpu::Adapter adapter = WebGPUUtils::RequestAdapterSync(instance, &adapterOptions);

    WebGPUUtils::InspectAdapter(adapter);

    // Requesting Device
    wgpu::DeviceDescriptor deviceDesc   = {};
    deviceDesc.nextInChain              = nullptr;
    deviceDesc.label                    = WebGPUUtils::GenerateString("My Device");
    deviceDesc.requiredFeatureCount     = 0;
    wgpu::RequiredLimits requiredLimits = GetRequiredLimits(adapter);
    deviceDesc.requiredLimits           = &requiredLimits;
    deviceDesc.defaultQueue.nextInChain = nullptr;
    deviceDesc.defaultQueue.label       = WebGPUUtils::GenerateString("The default queue");

#ifdef __EMSCRIPTEN__
    deviceDesc.deviceLostCallback =
        [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */)
    {
        SDL_Log("Device lost: reason 0x%08X", reason);
        if (message)
        {
            SDL_Log(" - message: %s", message);
        }
    };
#else
    auto deviceLostCallback = [](const wgpu::Device& device,
                                 wgpu::DeviceLostReason reason,
                                 const char* message,
                                 void* /* userData */)
    {
        SDL_Log("Device lost: reason 0x%08X", reason);
        if (message)
        {
            SDL_Log(" - message: %s", message);
        }
    };
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                                     deviceLostCallback,
                                     (void*)nullptr);

    auto uncapturedErrorCallback = [](const wgpu::Device& device,
                                      wgpu::ErrorType type,
                                      const char* message,
                                      void* /* pUserData */)
    {
        SDL_Log("Uncaptured device error: type 0x%08X", type);
        if (message)
        {
            SDL_Log(" - message: %s", message);
        }
    };

    deviceDesc.SetUncapturedErrorCallback(uncapturedErrorCallback, (void*)nullptr);
#endif

    device = WebGPUUtils::RequestDeviceSync(adapter, &deviceDesc);

#ifdef __EMSCRIPTEN__
    auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */)
    {
        SDL_Log("Uncaptured device error: type 0x%08X", type);
        if (message)
        {
            SDL_Log(" - message: %s", message);
        }
    };
    device.SetUncapturedErrorCallback(onDeviceError, (void*)nullptr);
#endif

    WebGPUUtils::InspectDevice(device);

    queue = device.GetQueue();

    surfaceFormat = WebGPUUtils::GetTextureFormat(surface, adapter);

    // Configure the surface
    wgpu::SurfaceConfiguration config = {};
    config.nextInChain                = nullptr;
    config.width                      = 1024;
    config.height                     = 768;
    config.usage                      = wgpu::TextureUsage::RenderAttachment;
    config.format                     = surfaceFormat;
    config.viewFormatCount            = 0;
    config.viewFormats                = nullptr;
    config.device                     = device;
    config.presentMode                = wgpu::PresentMode::Fifo;
    config.alphaMode                  = wgpu::CompositeAlphaMode::Auto;

    surface.Configure(&config);

    InitializePipeline();
    InitializeBuffers();

    return true;
}

void Application::Terminate()
{
    // Terminate SDL
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void Application::MainLoop()
{
    if (!isRunning)
    {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop(); /* this should "kill" the app. */
        Terminate();
        return;
#endif
    }

    // process input
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                isRunning = false;
                break;

            default:
                break;
        }
    }

    const Uint8* state = SDL_GetKeyboardState(NULL);
    if (state[SDL_SCANCODE_ESCAPE])
    {
        isRunning = false;
    }

#ifndef __EMSCRIPTEN__
    // Wait until 16ms has elapsed since last frame
    while (!SDL_TICKS_PASSED(SDL_GetTicks64(), tickCount + 16))
        ;
#endif
    float delta = (SDL_GetTicks64() - tickCount) / 1000.0f;
    deltaTime   = std::min(delta, 0.05f);
    tickCount   = SDL_GetTicks64();

    // Get the next target texture view
    wgpu::TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView)
    {
        return;
    }

    // Create a command encoder for the draw call
    wgpu::CommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain                    = nullptr;
    encoderDesc.label                          = WebGPUUtils::GenerateString("My command encoder");
    wgpu::CommandEncoder encoder               = device.CreateCommandEncoder(&encoderDesc);

    // Create the render pass that clears the screen with our color
    wgpu::RenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain                = nullptr;
    renderPassDesc.label                      = WebGPUUtils::GenerateString("Render Pass");

    // The attachment part of the render pass descriptor describes the target texture of the pass
    wgpu::RenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.nextInChain                     = nullptr;
    renderPassColorAttachment.view                            = targetView;
    renderPassColorAttachment.resolveTarget                   = nullptr;
    renderPassColorAttachment.loadOp                          = wgpu::LoadOp::Clear;
    renderPassColorAttachment.storeOp                         = wgpu::StoreOp::Store;
    renderPassColorAttachment.clearValue                      = {0.05, 0.05, 0.05, 1.0};
    renderPassColorAttachment.depthSlice                      = WGPU_DEPTH_SLICE_UNDEFINED;

    renderPassDesc.colorAttachmentCount   = 1;
    renderPassDesc.colorAttachments       = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites        = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDesc);

    // set pipeline to renderpass and draw
    renderPass.SetPipeline(pipeline);
    renderPass.SetVertexBuffer(0, pointBuffer, 0, pointBuffer.GetSize());
    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint16, 0, indexBuffer.GetSize());
    renderPass.DrawIndexed(indexCount, 1, 0, 0, 0);
    renderPass.End();

    // Finally encode and submit the render pass
    wgpu::CommandBufferDescriptor cmdBufferDescriptor;
    cmdBufferDescriptor.nextInChain = nullptr;
    cmdBufferDescriptor.label       = WebGPUUtils::GenerateString("Command buffer");

    wgpu::CommandBuffer command = encoder.Finish(&cmdBufferDescriptor);

    queue.Submit(1, &command);

#ifndef __EMSCRIPTEN__
    surface.Present();
    device.Tick();
#endif
}

bool Application::IsRunning()
{
    return isRunning;
}

wgpu::RequiredLimits Application::GetRequiredLimits(wgpu::Adapter adapter) const
{
    // Get adapter supported limits, in case we need them
    wgpu::SupportedLimits supportedLimits;
    supportedLimits.nextInChain = nullptr;
    adapter.GetLimits(&supportedLimits);

    wgpu::RequiredLimits requiredLimits {};
    SetDefaultLimits(requiredLimits.limits);

    requiredLimits.limits.maxVertexAttributes        = 2;
    requiredLimits.limits.maxVertexBuffers           = 2;
    requiredLimits.limits.maxBufferSize              = 5 * 5 * sizeof(float);
    requiredLimits.limits.maxVertexBufferArrayStride = 5 * sizeof(float);

    requiredLimits.limits.maxUniformBufferBindingSize =
        supportedLimits.limits.maxUniformBufferBindingSize;
    requiredLimits.limits.maxStorageBufferBindingSize =
        supportedLimits.limits.maxStorageBufferBindingSize;

    requiredLimits.limits.maxInterStageShaderComponents = 3;

    requiredLimits.limits.minUniformBufferOffsetAlignment =
        supportedLimits.limits.minUniformBufferOffsetAlignment;
    requiredLimits.limits.minStorageBufferOffsetAlignment =
        supportedLimits.limits.minStorageBufferOffsetAlignment;

    return requiredLimits;
}

void Application::InitializePipeline()
{
    // Load the shader module
    wgpu::ShaderModuleDescriptor shaderDesc {};

    wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc {};
    shaderCodeDesc.nextInChain = nullptr;
#ifdef __EMSCRIPTEN__
    shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
#else
    shaderCodeDesc.sType = wgpu::SType::ShaderSourceWGSL;
#endif
    // conect the chain
    shaderDesc.nextInChain          = &shaderCodeDesc;
    shaderCodeDesc.code             = WebGPUUtils::GenerateString(shaderSource);
    wgpu::ShaderModule shaderModule = device.CreateShaderModule(&shaderDesc);

    // Create the render pipeline
    wgpu::RenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.nextInChain                    = nullptr;

    // Describe vertex pipeline
    wgpu::VertexBufferLayout vertexBufferLayout {};
    std::vector<wgpu::VertexAttribute> positionAttribs(2);
    positionAttribs[0].shaderLocation = 0;  // @location(0) position attribute
    positionAttribs[0].format         = wgpu::VertexFormat::Float32x2;
    positionAttribs[0].offset         = 0;
    positionAttribs[1].shaderLocation = 1;  // @location(1) color attribute
    positionAttribs[1].format         = wgpu::VertexFormat::Float32x3;
    positionAttribs[1].offset         = 2 * sizeof(float);

    vertexBufferLayout.attributeCount = static_cast<uint32_t>(positionAttribs.size());
    vertexBufferLayout.attributes     = positionAttribs.data();
    vertexBufferLayout.arrayStride    = 5 * sizeof(float);
    vertexBufferLayout.stepMode       = wgpu::VertexStepMode::Vertex;

    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers     = &vertexBufferLayout;

    pipelineDesc.vertex.module        = shaderModule;
    pipelineDesc.vertex.entryPoint    = WebGPUUtils::GenerateString("vs_main");
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants     = nullptr;

    // Describe primitive pipeline
    pipelineDesc.primitive.topology         = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace        = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode         = wgpu::CullMode::None;

    // Describe fragment pipeline
    wgpu::FragmentState fragmentState {};
    fragmentState.module        = shaderModule;
    fragmentState.entryPoint    = WebGPUUtils::GenerateString("fs_main");
    fragmentState.constantCount = 0;
    fragmentState.constants     = nullptr;

    // blend settings
    wgpu::BlendState blendState {};
    blendState.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blendState.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blendState.color.operation = wgpu::BlendOperation::Add;
    blendState.alpha.srcFactor = wgpu::BlendFactor::Zero;
    blendState.alpha.dstFactor = wgpu::BlendFactor::One;
    blendState.alpha.operation = wgpu::BlendOperation::Add;

    wgpu::ColorTargetState colorTarget {};
    colorTarget.format    = surfaceFormat;
    colorTarget.blend     = &blendState;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    fragmentState.targetCount = 1;
    fragmentState.targets     = &colorTarget;
    pipelineDesc.fragment     = &fragmentState;

    // Describe stencil/depth pipeline
    pipelineDesc.depthStencil = nullptr;

    // Describe multi-sampling pipeline
    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Describe pipeline layout
    pipelineDesc.layout = nullptr;

    pipeline = device.CreateRenderPipeline(&pipelineDesc);
}

void Application::InitializeBuffers()
{
    // Define point data
    std::vector<float> pointData = {
        // x,   y,     r,   g,   b
        -0.5, -0.5, 1.0, 0.0, 0.0,  // Point #0
        +0.5, -0.5, 0.0, 1.0, 0.0,  // Point #1
        +0.5, +0.5, 0.0, 0.0, 1.0,  // Point #2
        -0.5, +0.5, 1.0, 1.0, 0.0   // Point #3
    };

    // Define index data
    std::vector<uint16_t> indexData = {0,  // Triangle #0 connects points #0, #1 and #2
                                       1,
                                       2,
                                       0,  // Triangle #1 connects points #0, #2 and #3
                                       2,
                                       3};

    indexCount = static_cast<uint32_t>(indexData.size());

    // Create vertex buffer
    wgpu::BufferDescriptor bufferDesc {};
    bufferDesc.nextInChain      = nullptr;
    bufferDesc.size             = pointData.size() * sizeof(float);
    bufferDesc.usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
    bufferDesc.mappedAtCreation = false;
    pointBuffer                 = device.CreateBuffer(&bufferDesc);

    queue.WriteBuffer(pointBuffer, 0, pointData.data(), bufferDesc.size);

    // Create index buffer
    bufferDesc.size  = indexData.size() * sizeof(uint16_t);
    bufferDesc.size  = (bufferDesc.size + 3) & ~3;  // round up to the next multiple of 4
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
    indexBuffer      = device.CreateBuffer(&bufferDesc);

    queue.WriteBuffer(indexBuffer, 0, indexData.data(), bufferDesc.size);
}

wgpu::TextureView Application::GetNextSurfaceTextureView()
{
    // Get the surface texture
    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Success)
    {
        return nullptr;
    }

    // Create a view for this surface texture
    wgpu::TextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label       = WebGPUUtils::GenerateString("Surface texture view");
#ifndef __EMSCRIPTEN__
    viewDescriptor.usage = wgpu::TextureUsage::RenderAttachment;
#endif
    viewDescriptor.format          = surfaceTexture.texture.GetFormat();
    viewDescriptor.dimension       = wgpu::TextureViewDimension::e2D;
    viewDescriptor.baseMipLevel    = 0;
    viewDescriptor.mipLevelCount   = 1;
    viewDescriptor.baseArrayLayer  = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect          = wgpu::TextureAspect::All;

    wgpu::TextureView targetView = surfaceTexture.texture.CreateView(&viewDescriptor);

    return targetView;
}

void Application::SetDefaultLimits(wgpu::Limits& limits) const
{
    limits.maxTextureDimension1D                     = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxTextureDimension2D                     = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxTextureDimension3D                     = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindGroups                             = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindGroupsPlusVertexBuffers            = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindingsPerBindGroup                   = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxDynamicUniformBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxDynamicStorageBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxSampledTexturesPerShaderStage          = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxSamplersPerShaderStage                 = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxStorageBuffersPerShaderStage           = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxStorageTexturesPerShaderStage          = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxUniformBuffersPerShaderStage           = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxUniformBufferBindingSize               = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxStorageBufferBindingSize               = WGPU_LIMIT_U32_UNDEFINED;
    limits.minUniformBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
    limits.minStorageBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxVertexBuffers                          = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBufferSize                             = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxVertexAttributes                       = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxVertexBufferArrayStride                = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxInterStageShaderComponents             = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxInterStageShaderVariables              = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxColorAttachments                       = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxColorAttachmentBytesPerSample          = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupStorageSize            = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeInvocationsPerWorkgroup         = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupSizeX                  = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupSizeY                  = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupSizeZ                  = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupsPerDimension          = WGPU_LIMIT_U32_UNDEFINED;
}
