#include "Application.h"

#include <vector>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

#include "ResourceManager.h"
#include "WebGPUUtils.h"
#include "sdl3webgpu.h"

constexpr float PI = 3.14159265358979323846f;

namespace ImGui
{
    bool DragDirection(const char* label, glm::vec4& direction)
    {
        glm::vec2 angles = glm::degrees(glm::polar(glm::vec3(direction)));
        bool changed     = ImGui::DragFloat2(label, glm::value_ptr(angles));
        direction        = glm::vec4(glm::euclidean(glm::radians(angles)), direction.w);
        return changed;
    }
}  // namespace ImGui

bool Application::Initialize()
{
    return InitializeWindowAndDevice() && InitializeDepthBuffer() && InitializeBindGroupLayout()
           && InitializePipeline() && InitializeTexture() && InitializeGeometry()
           && InitializeUniforms() && InitializeLightingUniforms() && InitializeBindGroups()
           && InitializeGUI();
}

void Application::Terminate()
{
    // Terminate GUI
    TerminateGUI();

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
            case SDL_EVENT_QUIT:
                isRunning = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                OnMouseMove();
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                OnScroll(event);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                OnMouseButton(event);
                break;

            default:
                break;
        }

        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    const bool* state = SDL_GetKeyboardState(nullptr);
    if (state[SDL_SCANCODE_ESCAPE])
    {
        isRunning = false;
    }

#ifndef __EMSCRIPTEN__
    // Wait until 16ms has elapsed since last frame
    while (SDL_GetTicks() < tickCount + 16)
        ;
#endif
    float delta = (SDL_GetTicks() - tickCount) / 1000.0f;
    deltaTime   = std::min(delta, 0.05f);
    tickCount   = SDL_GetTicks();

    // Update uniform buffer
    UpdateLightingUniforms();

    uniforms.time = tickCount / 1000.0f;
    queue.WriteBuffer(uniformBuffer,
                      offsetof(MyUniforms, time),
                      &uniforms.time,
                      sizeof(MyUniforms::time));

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

    UpdateGUI(renderPass);

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

bool Application::InitializeWindowAndDevice()
{
    // Init SDL
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Dawn Sample", 1024, 768, 0);

    // Create instance
    static const auto kTimeoutWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor desc     = {};
    desc.nextInChain                  = nullptr;
    desc.requiredFeatureCount         = 1;
    desc.requiredFeatures             = &kTimeoutWaitAny;

#ifndef __EMSCRIPTEN__
    wgpu::DawnTogglesDescriptor toggles;
    toggles.nextInChain         = nullptr;
    toggles.sType               = wgpu::SType::DawnTogglesDescriptor;
    toggles.disabledToggleCount = 0;
    toggles.enabledToggleCount  = 1;
    const char* toggleName      = "enable::immediate::error::handling";
    toggles.enabledToggles      = &toggleName;

    desc.nextInChain = &toggles;
#endif

    wgpu::Instance instance = wgpu::CreateInstance(&desc);

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
    wgpu::DeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain            = nullptr;
    deviceDesc.label                  = WebGPUUtils::GenerateString("My Device");
    deviceDesc.requiredFeatureCount   = 0;

    wgpu::Limits requiredLimits         = GetRequiredLimits(adapter);
    deviceDesc.requiredLimits           = &requiredLimits;
    deviceDesc.defaultQueue.nextInChain = nullptr;
    deviceDesc.defaultQueue.label       = WebGPUUtils::GenerateString("The default queue");

    auto deviceLostCallback =
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
    {
        SDL_Log("Device lost: reason 0x%08X", reason);
        if (message.data)
        {
            SDL_Log(" - message: %s", message.data);
        }
    };

    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous, deviceLostCallback);

    auto uncapturedErrorCallback =
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message)
    {
        SDL_Log("Uncaptured device error: type 0x%08X", type);
        if (message.data)
        {
            SDL_Log(" - message: %s", message.data);
        }
    };

    deviceDesc.SetUncapturedErrorCallback(uncapturedErrorCallback);

    device = WebGPUUtils::RequestDeviceSync(instance, adapter, &deviceDesc);

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

    return true;
}

bool Application::InitializeDepthBuffer()
{
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

    return depthTextureView != nullptr;
}

bool Application::InitializeBindGroupLayout()
{
    std::vector<wgpu::BindGroupLayoutEntry> bindingLayoutEntries(5);

    // The uniform buffer binding
    wgpu::BindGroupLayoutEntry& bindingLayout = bindingLayoutEntries[0];
    SetDefaultBindGroupLayout(bindingLayout);
    bindingLayout.binding               = 0;
    bindingLayout.visibility            = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    bindingLayout.buffer.type           = wgpu::BufferBindingType::Uniform;
    bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

    // The texture binding
    wgpu::BindGroupLayoutEntry& textureBindingLayout = bindingLayoutEntries[1];
    SetDefaultBindGroupLayout(textureBindingLayout);
    textureBindingLayout.binding               = 1;
    textureBindingLayout.visibility            = wgpu::ShaderStage::Fragment;
    textureBindingLayout.texture.sampleType    = wgpu::TextureSampleType::Float;
    textureBindingLayout.texture.viewDimension = wgpu::TextureViewDimension::e2D;

    // The texture binding
    wgpu::BindGroupLayoutEntry& normalTextureBindingLayout = bindingLayoutEntries[2];
    SetDefaultBindGroupLayout(normalTextureBindingLayout);
    normalTextureBindingLayout.binding               = 2;
    normalTextureBindingLayout.visibility            = wgpu::ShaderStage::Fragment;
    normalTextureBindingLayout.texture.sampleType    = wgpu::TextureSampleType::Float;
    normalTextureBindingLayout.texture.viewDimension = wgpu::TextureViewDimension::e2D;

    // The texture sampler binding
    wgpu::BindGroupLayoutEntry& samplerBindingLayout = bindingLayoutEntries[3];
    SetDefaultBindGroupLayout(samplerBindingLayout);
    samplerBindingLayout.binding      = 3;
    samplerBindingLayout.visibility   = wgpu::ShaderStage::Fragment;
    samplerBindingLayout.sampler.type = wgpu::SamplerBindingType::Filtering;

    // The lighting uniform buffer binding
    wgpu::BindGroupLayoutEntry& lightingUniformLayout = bindingLayoutEntries[4];
    SetDefaultBindGroupLayout(lightingUniformLayout);
    lightingUniformLayout.binding               = 4;
    lightingUniformLayout.visibility            = wgpu::ShaderStage::Fragment;
    lightingUniformLayout.buffer.type           = wgpu::BufferBindingType::Uniform;
    lightingUniformLayout.buffer.minBindingSize = sizeof(LightingUniforms);

    // Create a bind group layout
    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc {};
    bindGroupLayoutDesc.entryCount = static_cast<uint32_t>(bindingLayoutEntries.size());
    bindGroupLayoutDesc.entries    = bindingLayoutEntries.data();
    bindGroupLayout                = device.CreateBindGroupLayout(&bindGroupLayoutDesc);

    return bindGroupLayout != nullptr;
}

wgpu::Limits Application::GetRequiredLimits(wgpu::Adapter adapter) const
{
    // Get adapter supported limits, in case we need them
    wgpu::Limits supportedLimits;
    supportedLimits.nextInChain = nullptr;
    adapter.GetLimits(&supportedLimits);

    wgpu::Limits requiredLimits {};
    SetDefaultLimits(requiredLimits);

    requiredLimits.maxVertexAttributes        = 6;
    requiredLimits.maxVertexBuffers           = 2;
    requiredLimits.maxBufferSize              = 150000 * sizeof(VertexAttributes);
    requiredLimits.maxVertexBufferArrayStride = sizeof(VertexAttributes);

    requiredLimits.maxBindGroups                   = 2;
    requiredLimits.maxUniformBuffersPerShaderStage = 2;
    requiredLimits.maxUniformBufferBindingSize     = 16 * 4 * sizeof(float);

    requiredLimits.maxStorageBufferBindingSize = supportedLimits.maxStorageBufferBindingSize;

    requiredLimits.maxTextureDimension1D = 2048;
    requiredLimits.maxTextureDimension2D = 2048;
    requiredLimits.maxTextureArrayLayers = 1;

    requiredLimits.maxSampledTexturesPerShaderStage = 2;
    requiredLimits.maxSamplersPerShaderStage        = 1;

    requiredLimits.minUniformBufferOffsetAlignment =
        supportedLimits.minUniformBufferOffsetAlignment;
    requiredLimits.minStorageBufferOffsetAlignment =
        supportedLimits.minStorageBufferOffsetAlignment;

    return requiredLimits;
}

bool Application::InitializePipeline()
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
    std::vector<wgpu::VertexAttribute> vertexAttribs(6);
    vertexAttribs[0].shaderLocation = 0;  // @location(0) position attribute
    vertexAttribs[0].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[0].offset         = offsetof(VertexAttributes, position);
    vertexAttribs[1].shaderLocation = 1;  // @location(1) tangent attribute
    vertexAttribs[1].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[1].offset         = offsetof(VertexAttributes, position);
    vertexAttribs[2].shaderLocation = 2;  // @location(2) bitangent attribute
    vertexAttribs[2].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[2].offset         = offsetof(VertexAttributes, position);
    vertexAttribs[3].shaderLocation = 3;  // @location(3) normal attribute
    vertexAttribs[3].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[3].offset         = offsetof(VertexAttributes, normal);
    vertexAttribs[4].shaderLocation = 4;  // @location(4) color attribute
    vertexAttribs[4].format         = wgpu::VertexFormat::Float32x3;
    vertexAttribs[4].offset         = offsetof(VertexAttributes, color);
    vertexAttribs[5].shaderLocation = 5;  // @location(5) uv attribute
    vertexAttribs[5].format         = wgpu::VertexFormat::Float32x2;
    vertexAttribs[5].offset         = offsetof(VertexAttributes, uv);

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
    depthStencilState.depthCompare      = wgpu::CompareFunction::Less;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.format            = depthTextureFormat;
    depthStencilState.stencilReadMask   = 0;
    depthStencilState.stencilWriteMask  = 0;
    pipelineDesc.depthStencil           = &depthStencilState;

    // Describe multi-sampling pipeline
    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Describe pipeline layout
    wgpu::PipelineLayoutDescriptor layoutDesc {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts     = (wgpu::BindGroupLayout*)&bindGroupLayout;
    layout                          = device.CreatePipelineLayout(&layoutDesc);

    pipelineDesc.layout = layout;

    pipeline = device.CreateRenderPipeline(&pipelineDesc);

    return pipeline != nullptr;
}

bool Application::InitializeTexture()
{
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

    // Create a texture
    baseColorTexture = ResourceManager::LoadTexture("resources/fourareen2K_albedo.jpg",
                                                    device,
                                                    &baseColorTextureView);

    normalTexture = ResourceManager::LoadTexture("resources/fourareen2K_normals.png",
                                                 device,
                                                 &normalTextureView);

    if (!baseColorTexture || !normalTexture)
    {
        SDL_Log("Could not load texture!");
        return false;
    }
    return true;
}

bool Application::InitializeGeometry()
{
    // Load mesh data from OBJ file
    std::vector<VertexAttributes> vertexData;

    bool success = ResourceManager::LoadGeometryFromObj("resources/fourareen.obj", vertexData);

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

    return pointBuffer != nullptr;
}

bool Application::InitializeUniforms()
{
    // Create uniform buffer
    wgpu::BufferDescriptor bufferDesc {};
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

    return uniformBuffer != nullptr;
}

bool Application::InitializeBindGroups()
{
    // Create a binding
    std::vector<wgpu::BindGroupEntry> bindings(5);
    bindings[0].binding = 0;
    bindings[0].buffer  = uniformBuffer;
    bindings[0].offset  = 0;
    bindings[0].size    = sizeof(MyUniforms);

    bindings[1].binding     = 1;
    bindings[1].textureView = baseColorTextureView;

    bindings[2].binding     = 2;
    bindings[2].textureView = normalTextureView;

    bindings[3].binding = 3;
    bindings[3].sampler = sampler;

    bindings[4].binding = 4;
    bindings[4].buffer  = lightingUniformBuffer;
    bindings[4].offset  = 0;
    bindings[4].size    = sizeof(LightingUniforms);

    // A bind group contains one or multiple bindings
    wgpu::BindGroupDescriptor bindGroupDesc {};
    bindGroupDesc.layout     = bindGroupLayout;
    bindGroupDesc.entryCount = static_cast<uint32_t>(bindings.size());
    bindGroupDesc.entries    = bindings.data();
    bindGroup                = device.CreateBindGroup(&bindGroupDesc);

    return bindGroup != nullptr;
}

bool Application::InitializeGUI()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO();

    // Get the surface texture
    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureFormat format = surfaceTexture.texture.GetFormat();

    // Describe multi-sampling pipeline
    WGPUMultisampleState multiSampleState;
    multiSampleState.count                  = 1;
    multiSampleState.mask                   = ~0u;
    multiSampleState.alphaToCoverageEnabled = false;

    // Setup platform/Renderer backends
    ImGui_ImplSDL3_InitForOther(window);
    ImGui_ImplWGPU_InitInfo initInfo;
    initInfo.Device                   = device.Get();
    initInfo.DepthStencilFormat       = (WGPUTextureFormat)depthTextureFormat;
    initInfo.RenderTargetFormat       = (WGPUTextureFormat)format;
    initInfo.PipelineMultisampleState = multiSampleState;
    initInfo.NumFramesInFlight        = 3;
    ImGui_ImplWGPU_Init(&initInfo);
    return true;
}

void Application::TerminateGUI()
{
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void Application::UpdateGUI(wgpu::RenderPassEncoder renderPass)
{
    // Start the Dear ImGui frame
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Build UI
    {
        bool changed = false;
        ImGui::Begin("Lighting");
        changed =
            ImGui::ColorEdit3("Color #0", glm::value_ptr(lightingUniforms.colors[0])) || changed;
        changed = ImGui::DragDirection("Direction #0", lightingUniforms.directions[0]) || changed;
        changed =
            ImGui::ColorEdit3("Color #1", glm::value_ptr(lightingUniforms.colors[1])) || changed;
        changed = ImGui::DragDirection("Direction #1", lightingUniforms.directions[1]) || changed;
        changed =
            ImGui::SliderFloat("Hardness", &lightingUniforms.hardness, 1.0f, 100.0f) || changed;
        changed = ImGui::SliderFloat("K Diffuse", &lightingUniforms.kd, 0.0f, 1.0f) || changed;
        changed = ImGui::SliderFloat("K Specular", &lightingUniforms.ks, 0.0f, 1.0f) || changed;
        ImGui::End();
        lightingUniformsChanged = changed;
    }

    // Draw UI
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass.Get());
}

wgpu::TextureView Application::GetNextSurfaceTextureView()
{
    // Get the surface texture
    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
        && surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
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

void Application::UpdateViewMatrix()
{
    float cx            = std::cos(cameraState.angles.x);
    float sx            = std::sin(cameraState.angles.x);
    float cy            = std::cos(cameraState.angles.y);
    float sy            = std::sin(cameraState.angles.y);
    glm::vec3 position  = glm::vec3(cx * cy, sx * cy, sy) * std::exp(-cameraState.zoom);
    uniforms.viewMatrix = glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0, 0, 1));
    queue.WriteBuffer(uniformBuffer,
                      offsetof(MyUniforms, viewMatrix),
                      &uniforms.viewMatrix,
                      sizeof(MyUniforms::viewMatrix));

    uniforms.cameraWorldPosition = position;
    queue.WriteBuffer(uniformBuffer,
                      offsetof(MyUniforms, cameraWorldPosition),
                      &uniforms.cameraWorldPosition,
                      sizeof(MyUniforms::cameraWorldPosition));
}

void Application::OnMouseMove()
{
    if (!dragState.active)
    {
        return;
    }

    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);

    glm::vec2 currentMouse = glm::vec2(x, y);
    glm::vec2 delta        = (currentMouse - dragState.startMouse) * dragState.sensitivity;
    cameraState.angles     = dragState.startCameraState.angles + delta;
    // Clamp to avoid going too far when orbitting up/down
    cameraState.angles.y = glm::clamp(cameraState.angles.y, -PI / 2 + 1e-5f, PI / 2 - 1e-5f);
    UpdateViewMatrix();

    // Inertia
    dragState.velocity      = delta - dragState.previousDelta;
    dragState.previousDelta = delta;
}

void Application::OnMouseButton(SDL_Event& event)
{
    assert(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP);

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        return;
    }

    if (event.button.button != SDL_BUTTON_LEFT)
    {
        return;
    }

    bool isPressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? true : false;
    if (isPressed)
    {
        dragState.active = true;
        float x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        dragState.startMouse       = glm::vec2(-x, y);
        dragState.startCameraState = cameraState;
    }
    else
    {
        dragState.active = false;
    }
}

void Application::OnScroll(SDL_Event& event)
{
    assert(event.type == SDL_EVENT_MOUSE_WHEEL);

    cameraState.zoom += dragState.scrollSensitivity * static_cast<float>(event.wheel.y);
    cameraState.zoom = glm::clamp(cameraState.zoom, -2.0f, 2.0f);
    UpdateViewMatrix();
}

bool Application::InitializeLightingUniforms()
{
    // Create uniform buffer
    wgpu::BufferDescriptor bufferDesc;
    bufferDesc.size             = sizeof(LightingUniforms);
    bufferDesc.usage            = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
    bufferDesc.mappedAtCreation = false;
    lightingUniformBuffer       = device.CreateBuffer(&bufferDesc);

    // Initialize values
    lightingUniforms.directions[0] = {0.5f, -0.9f, 0.1f, 0.0f};
    lightingUniforms.directions[1] = {0.2f, 0.4f, 0.3f, 0.0f};
    lightingUniforms.colors[0]     = {1.0f, 0.9f, 0.6f, 1.0f};
    lightingUniforms.colors[1]     = {0.6f, 0.9f, 1.0f, 1.0f};

    UpdateLightingUniforms();

    return lightingUniformBuffer != nullptr;
}

void Application::UpdateLightingUniforms()
{
    if (lightingUniformsChanged)
    {
        queue.WriteBuffer(lightingUniformBuffer, 0, &lightingUniforms, sizeof(LightingUniforms));
        lightingUniformsChanged = false;
    }
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
