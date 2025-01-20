#include "Application.h"

#include <vector>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "ResourceManager.h"
#include "WebGPUUtils.h"
#include "sdl2webgpu.h"

constexpr float PI = 3.14159265358979323846f;

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
    InitializeBindGroups();

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

    // Update uniform buffer
    uniforms.time = tickCount / 1000.0f;
    queue.WriteBuffer(uniformBuffer,
                      offsetof(MyUniforms, time),
                      &uniforms.time,
                      sizeof(MyUniforms::time));

    float viewZ = glm::mix(0.0f, 0.25f, cos(2 * PI * uniforms.time / 4) * 0.5 + 0.5);
    uniforms.viewMatrix =
        glm::lookAt(glm::vec3(-0.5f, -1.5f, viewZ + 0.25f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    queue.WriteBuffer(uniformBuffer,
                      offsetof(MyUniforms, viewMatrix),
                      &uniforms.viewMatrix,
                      sizeof(MyUniforms::viewMatrix));

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

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments     = &renderPassColorAttachment;

    wgpu::RenderPassDepthStencilAttachment depthStencilAttachment;
    depthStencilAttachment.view              = depthTextureView;
    depthStencilAttachment.depthClearValue   = 1.0f;
    depthStencilAttachment.depthLoadOp       = wgpu::LoadOp::Clear;
    depthStencilAttachment.depthStoreOp      = wgpu::StoreOp::Store;
    depthStencilAttachment.depthReadOnly     = false;
    depthStencilAttachment.stencilClearValue = 0;
    depthStencilAttachment.stencilLoadOp     = wgpu::LoadOp::Undefined;
    depthStencilAttachment.stencilStoreOp    = wgpu::StoreOp::Undefined;
    depthStencilAttachment.stencilReadOnly   = true;
    renderPassDesc.depthStencilAttachment    = &depthStencilAttachment;

    renderPassDesc.timestampWrites = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    wgpu::RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDesc);

    // set pipeline to renderpass and draw
    renderPass.SetPipeline(pipeline);
    renderPass.SetVertexBuffer(0, pointBuffer, 0, pointBuffer.GetSize());
    renderPass.SetBindGroup(0, bindGroup, 0, nullptr);
    renderPass.Draw(indexCount, 1, 0, 0);
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

    requiredLimits.limits.maxVertexAttributes        = 4;
    requiredLimits.limits.maxVertexBuffers           = 2;
    requiredLimits.limits.maxBufferSize              = 10000 * sizeof(VertexAttributes);
    requiredLimits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);

    requiredLimits.limits.maxBindGroups                   = 1;
    requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
    requiredLimits.limits.maxUniformBufferBindingSize     = 16 * 4 * sizeof(float);

    requiredLimits.limits.maxInterStageShaderComponents = 8;

    requiredLimits.limits.maxStorageBufferBindingSize =
        supportedLimits.limits.maxStorageBufferBindingSize;

    requiredLimits.limits.maxTextureDimension1D = 1024;
    requiredLimits.limits.maxTextureDimension2D = 768;
    requiredLimits.limits.maxTextureArrayLayers = 1;

    requiredLimits.limits.maxSampledTexturesPerShaderStage = 1;
    requiredLimits.limits.maxSamplersPerShaderStage        = 1;

    requiredLimits.limits.minUniformBufferOffsetAlignment =
        supportedLimits.limits.minUniformBufferOffsetAlignment;
    requiredLimits.limits.minStorageBufferOffsetAlignment =
        supportedLimits.limits.minStorageBufferOffsetAlignment;

    return requiredLimits;
}

void Application::InitializePipeline()
{
    // Load the shader module
    wgpu::ShaderModule shaderModule =
        ResourceManager::LoadShaderModule("resources/shader.wgsl", device);

    if (shaderModule == nullptr)
    {
        SDL_Log("Could not load shader!");
        exit(EXIT_FAILURE);
    }

    // Create the render pipeline
    wgpu::RenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.nextInChain                    = nullptr;

    // Describe vertex pipeline
    wgpu::VertexBufferLayout vertexBufferLayout {};
    std::vector<wgpu::VertexAttribute> vertexAttribs(4);
    vertexAttribs[0].shaderLocation = 0;  // @location(0) position attribute
    vertexAttribs[0].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[0].offset         = offsetof(VertexAttributes, position);
    vertexAttribs[1].shaderLocation = 1;  // @location(1) normal attribute
    vertexAttribs[1].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[1].offset         = offsetof(VertexAttributes, normal);
    vertexAttribs[2].shaderLocation = 2;  // @location(2) color attribute
    vertexAttribs[2].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[2].offset         = offsetof(VertexAttributes, color);
    vertexAttribs[3].shaderLocation = 3;  // @location(3) uv attribute
    vertexAttribs[3].format         = wgpu::VertexFormat::Float32x2;
    vertexAttribs[3].offset         = offsetof(VertexAttributes, uv);

    vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
    vertexBufferLayout.attributes     = vertexAttribs.data();
    vertexBufferLayout.arrayStride    = sizeof(VertexAttributes);
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
    wgpu::DepthStencilState depthStencilState;
    SetDefaultDepthStencilState(depthStencilState);
    depthStencilState.depthCompare         = wgpu::CompareFunction::Less;
    depthStencilState.depthWriteEnabled    = true;
    wgpu::TextureFormat depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
    depthStencilState.format               = depthTextureFormat;
    depthStencilState.stencilReadMask      = 0;
    depthStencilState.stencilWriteMask     = 0;
    pipelineDesc.depthStencil              = &depthStencilState;

    // Describe multi-sampling pipeline
    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Describe pipeline layout
    // Create a binding layout
    std::vector<wgpu::BindGroupLayoutEntry> bindingLayoutEntries(3);
    wgpu::BindGroupLayoutEntry& bindingLayout = bindingLayoutEntries[0];
    SetDefaultBindGroupLayout(bindingLayout);
    bindingLayout.binding               = 0;
    bindingLayout.visibility            = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    bindingLayout.buffer.type           = wgpu::BufferBindingType::Uniform;
    bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);
    wgpu::BindGroupLayoutEntry& textureBindingLayout = bindingLayoutEntries[1];
    SetDefaultBindGroupLayout(textureBindingLayout);
    textureBindingLayout.binding                     = 1;
    textureBindingLayout.visibility                  = wgpu::ShaderStage::Fragment;
    textureBindingLayout.texture.sampleType          = wgpu::TextureSampleType::Float;
    textureBindingLayout.texture.viewDimension       = wgpu::TextureViewDimension::e2D;
    wgpu::BindGroupLayoutEntry& samplerBindingLayout = bindingLayoutEntries[2];
    SetDefaultBindGroupLayout(samplerBindingLayout);
    samplerBindingLayout.binding      = 2;
    samplerBindingLayout.visibility   = wgpu::ShaderStage::Fragment;
    samplerBindingLayout.sampler.type = wgpu::SamplerBindingType::Filtering;

    // Create a bind group layout
    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc {};
    bindGroupLayoutDesc.entryCount = static_cast<uint32_t>(bindingLayoutEntries.size());
    bindGroupLayoutDesc.entries    = bindingLayoutEntries.data();
    bindGroupLayout                = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

    // Create pipeline layout
    wgpu::PipelineLayoutDescriptor layoutDesc {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts     = (wgpu::BindGroupLayout*)&bindGroupLayout;
    layout                          = device.CreatePipelineLayout(&layoutDesc);

    pipelineDesc.layout = layout;

    pipeline = device.CreateRenderPipeline(&pipelineDesc);

    // Create the depth texture
    wgpu::TextureDescriptor depthTextureDesc;
    depthTextureDesc.dimension       = wgpu::TextureDimension::e2D;
    depthTextureDesc.format          = depthTextureFormat;
    depthTextureDesc.mipLevelCount   = 1;
    depthTextureDesc.sampleCount     = 1;
    depthTextureDesc.size            = {1024, 768, 1};
    depthTextureDesc.usage           = wgpu::TextureUsage::RenderAttachment;
    depthTextureDesc.viewFormatCount = 1;
    depthTextureDesc.viewFormats     = (wgpu::TextureFormat*)&depthTextureFormat;
    depthTexture                     = device.CreateTexture(&depthTextureDesc);

    // Create the view of the depth texture manipulated by the rasterizer
    wgpu::TextureViewDescriptor depthTextureViewDesc;
    depthTextureViewDesc.nextInChain     = nullptr;
    depthTextureViewDesc.label           = WebGPUUtils::GenerateString("Depth Texture");
    depthTextureViewDesc.aspect          = wgpu::TextureAspect::DepthOnly;
    depthTextureViewDesc.baseArrayLayer  = 0;
    depthTextureViewDesc.arrayLayerCount = 1;
    depthTextureViewDesc.baseMipLevel    = 0;
    depthTextureViewDesc.mipLevelCount   = 1;
    depthTextureViewDesc.dimension       = wgpu::TextureViewDimension::e2D;
    depthTextureViewDesc.format          = depthTextureFormat;
    depthTextureView                     = depthTexture.CreateView(&depthTextureViewDesc);

    // Create the color texture
    wgpu::TextureDescriptor textureDesc;
    textureDesc.dimension       = wgpu::TextureDimension::e2D;
    textureDesc.size            = {256, 256, 1};
    textureDesc.mipLevelCount   = 8;
    textureDesc.sampleCount     = 1;
    textureDesc.format          = wgpu::TextureFormat::RGBA8Unorm;
    textureDesc.usage           = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    textureDesc.viewFormatCount = 0;
    textureDesc.viewFormats     = nullptr;
    texture                     = device.CreateTexture(&textureDesc);

    wgpu::TextureViewDescriptor textureViewDesc;
    textureViewDesc.aspect          = wgpu::TextureAspect::All;
    textureViewDesc.baseArrayLayer  = 0;
    textureViewDesc.arrayLayerCount = 1;
    textureViewDesc.baseMipLevel    = 0;
    textureViewDesc.mipLevelCount   = textureDesc.mipLevelCount;
    textureViewDesc.dimension       = wgpu::TextureViewDimension::e2D;
    textureViewDesc.format          = textureDesc.format;
    textureView                     = texture.CreateView(&textureViewDesc);

    // Create a sampler
    wgpu::SamplerDescriptor samplerDesc;
    samplerDesc.addressModeU  = wgpu::AddressMode::Repeat;
    samplerDesc.addressModeV  = wgpu::AddressMode::Repeat;
    samplerDesc.addressModeW  = wgpu::AddressMode::ClampToEdge;
    samplerDesc.magFilter     = wgpu::FilterMode::Linear;
    samplerDesc.minFilter     = wgpu::FilterMode::Linear;
    samplerDesc.mipmapFilter  = wgpu::MipmapFilterMode::Linear;
    samplerDesc.lodMinClamp   = 0.0f;
    samplerDesc.lodMaxClamp   = 8.0f;
    samplerDesc.compare       = wgpu::CompareFunction::Undefined;
    samplerDesc.maxAnisotropy = 1;
    sampler                   = device.CreateSampler(&samplerDesc);

    // Create image data
    std::vector<uint8_t> pixels(4 * textureDesc.size.width * textureDesc.size.height);
    for (uint32_t i = 0; i < textureDesc.size.width; ++i)
    {
        for (uint32_t j = 0; j < textureDesc.size.height; ++j)
        {
            uint8_t* p = &pixels[4 * (j * textureDesc.size.width + i)];
            p[0]       = (i / 16) % 2 == (j / 16) % 2 ? 255 : 0;  // r
            p[1]       = ((i - j) / 16) % 2 == 0 ? 255 : 0;       // g
            p[2]       = ((i + j) / 16) % 2 == 0 ? 255 : 0;       // b
            p[3]       = 255;                                     // a
        }
    }

    // Upload texture data
    wgpu::ImageCopyTexture destination;
    destination.texture = texture;
    destination.origin  = {0, 0, 0};
    destination.aspect  = wgpu::TextureAspect::All;

    wgpu::TextureDataLayout source;
    source.offset       = 0;
    source.bytesPerRow  = 4 * textureDesc.size.width;
    source.rowsPerImage = textureDesc.size.height;

    queue.WriteTexture(&destination, pixels.data(), pixels.size(), &source, &textureDesc.size);
}

void Application::InitializeBuffers()
{
    // Load mesh data from OBJ file
    std::vector<VertexAttributes> vertexData;

    bool success = ResourceManager::LoadGeometryFromObj("resources/plane.obj", vertexData);

    if (!success)
    {
        SDL_Log("Could not load geometry!");
        exit(EXIT_FAILURE);
    }

    // Create vertex buffer
    wgpu::BufferDescriptor bufferDesc {};
    bufferDesc.nextInChain      = nullptr;
    bufferDesc.size             = vertexData.size() * sizeof(VertexAttributes);
    bufferDesc.usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
    bufferDesc.mappedAtCreation = false;
    pointBuffer                 = device.CreateBuffer(&bufferDesc);

    queue.WriteBuffer(pointBuffer, 0, vertexData.data(), bufferDesc.size);

    indexCount = static_cast<int>(vertexData.size());

    // Create uniform buffer
    bufferDesc.size             = sizeof(MyUniforms);
    bufferDesc.usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
    bufferDesc.mappedAtCreation = false;
    uniformBuffer               = device.CreateBuffer(&bufferDesc);

    // Upload the initial value of the uniforms
    uniforms.modelMatrix = glm::mat4x4(1.0);
    uniforms.viewMatrix =
        glm::lookAt(glm::vec3(-2.0f, -3.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    uniforms.projectionMatrix = glm::perspective(45 * PI / 180, 640.0f / 480.0f, 0.01f, 100.0f);
    uniforms.time             = 1.0f;
    uniforms.color            = {0.0f, 1.0f, 0.4f, 1.0f};
    queue.WriteBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));
}

void Application::InitializeBindGroups()
{
    // Create a binding
    std::vector<wgpu::BindGroupEntry> bindings(3);
    bindings[0].binding = 0;
    bindings[0].buffer  = uniformBuffer;
    bindings[0].offset  = 0;
    bindings[0].size    = sizeof(MyUniforms);

    bindings[1].binding     = 1;
    bindings[1].textureView = textureView;

    bindings[2].binding = 2;
    bindings[2].sampler = sampler;

    // A bind group contains one or multiple bindings
    wgpu::BindGroupDescriptor bindGroupDesc {};
    bindGroupDesc.layout     = bindGroupLayout;
    bindGroupDesc.entryCount = static_cast<uint32_t>(bindings.size());
    bindGroupDesc.entries    = bindings.data();
    bindGroup                = device.CreateBindGroup(&bindGroupDesc);
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

void Application::SetDefaultBindGroupLayout(wgpu::BindGroupLayoutEntry& bindingLayout)
{
#ifdef __EMSCRIPTEN__
    bindingLayout.buffer.nextInChain      = nullptr;
    bindingLayout.buffer.type             = wgpu::BufferBindingType::Undefined;
    bindingLayout.buffer.hasDynamicOffset = false;

    bindingLayout.sampler.nextInChain = nullptr;
    bindingLayout.sampler.type        = wgpu::SamplerBindingType::Undefined;

    bindingLayout.storageTexture.nextInChain   = nullptr;
    bindingLayout.storageTexture.access        = wgpu::StorageTextureAccess::Undefined;
    bindingLayout.storageTexture.format        = wgpu::TextureFormat::Undefined;
    bindingLayout.storageTexture.viewDimension = wgpu::TextureViewDimension::Undefined;

    bindingLayout.texture.nextInChain   = nullptr;
    bindingLayout.texture.multisampled  = false;
    bindingLayout.texture.sampleType    = wgpu::TextureSampleType::Undefined;
    bindingLayout.texture.viewDimension = wgpu::TextureViewDimension::Undefined;
#else
    bindingLayout.buffer.nextInChain      = nullptr;
    bindingLayout.buffer.type             = wgpu::BufferBindingType::BindingNotUsed;
    bindingLayout.buffer.hasDynamicOffset = false;

    bindingLayout.sampler.nextInChain = nullptr;
    bindingLayout.sampler.type        = wgpu::SamplerBindingType::BindingNotUsed;

    bindingLayout.storageTexture.nextInChain   = nullptr;
    bindingLayout.storageTexture.access        = wgpu::StorageTextureAccess::BindingNotUsed;
    bindingLayout.storageTexture.format        = wgpu::TextureFormat::Undefined;
    bindingLayout.storageTexture.viewDimension = wgpu::TextureViewDimension::Undefined;

    bindingLayout.texture.nextInChain   = nullptr;
    bindingLayout.texture.multisampled  = false;
    bindingLayout.texture.sampleType    = wgpu::TextureSampleType::BindingNotUsed;
    bindingLayout.texture.viewDimension = wgpu::TextureViewDimension::Undefined;
#endif
}

void Application::SetDefaultStencilFaceState(wgpu::StencilFaceState& stencilFaceState)
{
    stencilFaceState.compare     = wgpu::CompareFunction::Always;
    stencilFaceState.failOp      = wgpu::StencilOperation::Keep;
    stencilFaceState.depthFailOp = wgpu::StencilOperation::Keep;
    stencilFaceState.passOp      = wgpu::StencilOperation::Keep;
}

void Application::SetDefaultDepthStencilState(wgpu::DepthStencilState& depthStencilstate)
{
    depthStencilstate.format              = wgpu::TextureFormat::Undefined;
    depthStencilstate.depthWriteEnabled   = false;
    depthStencilstate.depthCompare        = wgpu::CompareFunction::Always;
    depthStencilstate.stencilReadMask     = 0xFFFFFFFF;
    depthStencilstate.stencilWriteMask    = 0xFFFFFFFF;
    depthStencilstate.depthBias           = 0;
    depthStencilstate.depthBiasSlopeScale = 0;
    depthStencilstate.depthBiasClamp      = 0;
    SetDefaultStencilFaceState(depthStencilstate.stencilFront);
    SetDefaultStencilFaceState(depthStencilstate.stencilBack);
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
