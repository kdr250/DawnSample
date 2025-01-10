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
    return InitializeWindowAndDevice() && InitializeDepthBuffer() && InitializePipeline()
           && InitializeTexture() && InitializeGeometry() && InitializeUniforms()
           && InitializeBindGroups();
}

void Application::Terminate()
{
    TerminateBindGroups();
    TerminateUniforms();
    TerminateGeometry();
    TerminateTexture();
    TerminatePipeline();
    TerminateDepthBuffer();
    TerminateWindowAndDevice();
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

            case SDL_MOUSEMOTION:
                OnMouseMove();
                break;

            case SDL_MOUSEWHEEL:
                OnScroll(event);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                OnMouseButton(event);
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

    // Update uniform buffer
    uniforms.time = SDL_GetTicks64() / 1000.0f;
    wgpuQueueWriteBuffer(queue,
                         uniformBuffer,
                         offsetof(MyUniforms, time),
                         &uniforms.time,
                         sizeof(MyUniforms::time));

    // Get the next target texture view
    WGPUTextureView targetView = GetNextSurfaceTextureView();
    if (!targetView)
    {
        return;
    }

    // Create a command encoder for the draw call
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain                  = nullptr;
    encoderDesc.label                        = WebGPUUtils::GenerateString("My command encoder");
    WGPUCommandEncoder encoder               = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Create the render pass that clears the screen with our color
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain              = nullptr;

    // The attachment part of the render pass descriptor describes the target texture of the pass
    WGPURenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view                          = targetView;
    renderPassColorAttachment.resolveTarget                 = nullptr;
    renderPassColorAttachment.loadOp                        = WGPULoadOp_Clear;
    renderPassColorAttachment.storeOp                       = WGPUStoreOp_Store;
    renderPassColorAttachment.clearValue                    = WGPUColor {0.05, 0.05, 0.05, 1.0};
    renderPassColorAttachment.depthSlice                    = WGPU_DEPTH_SLICE_UNDEFINED;

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments     = &renderPassColorAttachment;

    WGPURenderPassDepthStencilAttachment depthStencilAttachment;
    depthStencilAttachment.view              = depthTextureView;
    depthStencilAttachment.depthClearValue   = 1.0f;
    depthStencilAttachment.depthLoadOp       = WGPULoadOp_Clear;
    depthStencilAttachment.depthStoreOp      = WGPUStoreOp_Store;
    depthStencilAttachment.depthReadOnly     = false;
    depthStencilAttachment.stencilClearValue = 0;
    depthStencilAttachment.stencilLoadOp     = WGPULoadOp_Undefined;
    depthStencilAttachment.stencilStoreOp    = WGPUStoreOp_Undefined;
    depthStencilAttachment.stencilReadOnly   = true;

    renderPassDesc.depthStencilAttachment = &depthStencilAttachment;
    renderPassDesc.timestampWrites        = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // set pipeline to renderpass and draw
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass,
                                         0,
                                         vertexBuffer,
                                         0,
                                         wgpuBufferGetSize(vertexBuffer));
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(renderPass, indexCount, 1, 0, 0);

    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    // Finally encode and submit the render pass
    WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain                 = nullptr;
    cmdBufferDescriptor.label                       = WebGPUUtils::GenerateString("Command buffer");

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);

    // At the end of the frame
    wgpuTextureViewRelease(targetView);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(surface);
#endif

    // tick
    WebGPUUtils::DeviceTick(device);
}

bool Application::IsRunning()
{
    return isRunning;
}

bool Application::InitializeWindowAndDevice()
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
    WGPUInstance instance = wgpuCreateInstance(nullptr);
#else
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain            = nullptr;

    WGPUDawnTogglesDescriptor toggles;
    toggles.chain.next          = nullptr;
    toggles.chain.sType         = WGPUSType_DawnTogglesDescriptor;
    toggles.disabledToggleCount = 0;
    toggles.enabledToggleCount  = 1;
    const char* toggleName      = "enable_immediate_error_handling";
    toggles.enabledToggles      = &toggleName;

    desc.nextInChain = &toggles.chain;

    WGPUInstance instance = wgpuCreateInstance(&desc);
#endif
    if (instance == nullptr)
    {
        SDL_Log("Instance creation failed!");
        return false;
    }

    // Create WebGPU surface
    surface = SDL_GetWGPUSurface(instance, window);

    // Requesting Adapter
    WGPURequestAdapterOptions adapterOptions = {};
    adapterOptions.nextInChain               = nullptr;
    adapterOptions.compatibleSurface         = surface;
    WGPUAdapter adapter = WebGPUUtils::RequestAdapterSync(instance, &adapterOptions);

    WebGPUUtils::InspectAdapter(adapter);

    wgpuInstanceRelease(instance);

    // Requesting Device
    WGPUDeviceDescriptor deviceDesc     = {};
    deviceDesc.nextInChain              = nullptr;
    deviceDesc.label                    = WebGPUUtils::GenerateString("My Device");
    deviceDesc.requiredFeatureCount     = 0;
    WGPURequiredLimits requiredLimits   = GetRequiredLimits(adapter);
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
    deviceDesc.deviceLostCallbackInfo2.nextInChain = nullptr;
    deviceDesc.deviceLostCallbackInfo2.mode        = WGPUCallbackMode_WaitAnyOnly;
    deviceDesc.deviceLostCallbackInfo2.callback    = [](const WGPUDevice* device,
                                                     WGPUDeviceLostReason reason,
                                                     WGPUStringView message,
                                                     void* /* pUserData1 */,
                                                     void* /* pUserData2 */)
    {
        SDL_Log("Device lost: reason 0x%08X", reason);
        if (message.data)
        {
            SDL_Log(" - message: %s", message.data);
        }
    };
    deviceDesc.uncapturedErrorCallbackInfo2.nextInChain = nullptr;
    deviceDesc.uncapturedErrorCallbackInfo2.callback    = [](const WGPUDevice* device,
                                                          WGPUErrorType type,
                                                          WGPUStringView message,
                                                          void* /* pUserData1 */,
                                                          void* /* pUserData2 */)
    {
        SDL_Log("Uncaptured device error: type 0x%08X", type);
        if (message.data)
        {
            SDL_Log(" - message: %s", message.data);
        }
    };
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
    wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr /* pUserData */);
#endif

    WebGPUUtils::InspectDevice(device);

    queue = wgpuDeviceGetQueue(device);

    surfaceFormat = WebGPUUtils::GetTextureFormat(surface, adapter);

    // Configure the surface
    WGPUSurfaceConfiguration config = {};
    config.nextInChain              = nullptr;
    config.width                    = 1024;
    config.height                   = 768;
    config.usage                    = WGPUTextureUsage_RenderAttachment;
    config.format                   = surfaceFormat;
    config.viewFormatCount          = 0;
    config.viewFormats              = nullptr;
    config.device                   = device;
    config.presentMode              = WGPUPresentMode_Fifo;
    config.alphaMode                = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(surface, &config);

    wgpuAdapterRelease(adapter);

    return true;
}

void Application::TerminateWindowAndDevice()
{
    // Terminate WebGPU
    wgpuSurfaceUnconfigure(surface);
    wgpuSurfaceRelease(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);

    // Terminate SDL
    SDL_DestroyWindow(window);
    SDL_Quit();
}

bool Application::InitializeDepthBuffer()
{
    // Create the depth texture
    WGPUTextureDescriptor depthTextureDesc;
    depthTextureDesc.nextInChain     = nullptr;
    depthTextureDesc.label           = WebGPUUtils::GenerateString("Depth Texture");
    depthTextureDesc.dimension       = WGPUTextureDimension_2D;
    depthTextureDesc.format          = depthTextureFormat;
    depthTextureDesc.mipLevelCount   = 1;
    depthTextureDesc.sampleCount     = 1;
    depthTextureDesc.size            = {1024, 768, 1};
    depthTextureDesc.usage           = WGPUTextureUsage_RenderAttachment;
    depthTextureDesc.viewFormatCount = 1;
    depthTextureDesc.viewFormats     = (WGPUTextureFormat*)&depthTextureFormat;
    depthTexture                     = wgpuDeviceCreateTexture(device, &depthTextureDesc);

    // Create the view of the depth texture manipulated by the rasterizer
    WGPUTextureViewDescriptor depthTextureViewDesc;
    depthTextureViewDesc.nextInChain = nullptr;
    depthTextureViewDesc.label       = WebGPUUtils::GenerateString("Depth Texture View");
    depthTextureViewDesc.aspect      = WGPUTextureAspect_DepthOnly;
#ifndef __EMSCRIPTEN__
    depthTextureViewDesc.usage = WGPUTextureUsage_None;
#endif
    depthTextureViewDesc.baseArrayLayer  = 0;
    depthTextureViewDesc.arrayLayerCount = 1;
    depthTextureViewDesc.baseMipLevel    = 0;
    depthTextureViewDesc.mipLevelCount   = 1;
    depthTextureViewDesc.dimension       = WGPUTextureViewDimension_2D;
    depthTextureViewDesc.format          = depthTextureFormat;
    depthTextureView = wgpuTextureCreateView(depthTexture, &depthTextureViewDesc);

    return true;
}

void Application::TerminateDepthBuffer()
{
    wgpuTextureViewRelease(depthTextureView);
    wgpuTextureDestroy(depthTexture);
    wgpuTextureRelease(depthTexture);
}

WGPURequiredLimits Application::GetRequiredLimits(WGPUAdapter adapter) const
{
    // Get adapter supported limits, in case we need them
    WGPUSupportedLimits supportedLimits;
    supportedLimits.nextInChain = nullptr;
    wgpuAdapterGetLimits(adapter, &supportedLimits);

    WGPURequiredLimits requiredLimits {};
    SetDefaultLimits(requiredLimits.limits);

    // vertex and shader
    requiredLimits.limits.maxVertexAttributes           = 4;
    requiredLimits.limits.maxVertexBuffers              = 1;
    requiredLimits.limits.maxBufferSize                 = 150000 * sizeof(VertexAttributes);
    requiredLimits.limits.maxVertexBufferArrayStride    = sizeof(VertexAttributes);
    requiredLimits.limits.maxInterStageShaderComponents = 8;

    // uniform
    requiredLimits.limits.maxBindGroups                   = 1;
    requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
    requiredLimits.limits.maxUniformBufferBindingSize     = 16 * 4 * sizeof(float);

    // for the depth buffer
    requiredLimits.limits.maxTextureDimension1D = 2048;
    requiredLimits.limits.maxTextureDimension2D = 2048;
    requiredLimits.limits.maxTextureArrayLayers = 1;

    requiredLimits.limits.maxSamplersPerShaderStage = 1;

    requiredLimits.limits.minUniformBufferOffsetAlignment =
        supportedLimits.limits.minUniformBufferOffsetAlignment;
    requiredLimits.limits.minStorageBufferOffsetAlignment =
        supportedLimits.limits.minStorageBufferOffsetAlignment;

    return requiredLimits;
}

void Application::SetDefaultBindGroupLayout(WGPUBindGroupLayoutEntry& bindingLayout)
{
#ifdef __EMSCRIPTEN__
    bindingLayout.buffer.nextInChain      = nullptr;
    bindingLayout.buffer.type             = WGPUBufferBindingType_Undefined;
    bindingLayout.buffer.hasDynamicOffset = false;

    bindingLayout.sampler.nextInChain = nullptr;
    bindingLayout.sampler.type        = WGPUSamplerBindingType_Undefined;

    bindingLayout.storageTexture.nextInChain   = nullptr;
    bindingLayout.storageTexture.access        = WGPUStorageTextureAccess_Undefined;
    bindingLayout.storageTexture.format        = WGPUTextureFormat_Undefined;
    bindingLayout.storageTexture.viewDimension = WGPUTextureViewDimension_Undefined;

    bindingLayout.texture.nextInChain   = nullptr;
    bindingLayout.texture.multisampled  = false;
    bindingLayout.texture.sampleType    = WGPUTextureSampleType_Undefined;
    bindingLayout.texture.viewDimension = WGPUTextureViewDimension_Undefined;
#else
    bindingLayout.buffer.nextInChain      = nullptr;
    bindingLayout.buffer.type             = WGPUBufferBindingType_BindingNotUsed;
    bindingLayout.buffer.hasDynamicOffset = false;

    bindingLayout.sampler.nextInChain = nullptr;
    bindingLayout.sampler.type        = WGPUSamplerBindingType_BindingNotUsed;

    bindingLayout.storageTexture.nextInChain   = nullptr;
    bindingLayout.storageTexture.access        = WGPUStorageTextureAccess_BindingNotUsed;
    bindingLayout.storageTexture.format        = WGPUTextureFormat_Undefined;
    bindingLayout.storageTexture.viewDimension = WGPUTextureViewDimension_Undefined;

    bindingLayout.texture.nextInChain   = nullptr;
    bindingLayout.texture.multisampled  = false;
    bindingLayout.texture.sampleType    = WGPUTextureSampleType_BindingNotUsed;
    bindingLayout.texture.viewDimension = WGPUTextureViewDimension_Undefined;
#endif
}

void Application::SetDefaultStencilFaceState(WGPUStencilFaceState& stencilFaceState)
{
    stencilFaceState.compare     = WGPUCompareFunction_Always;
    stencilFaceState.failOp      = WGPUStencilOperation_Keep;
    stencilFaceState.depthFailOp = WGPUStencilOperation_Keep;
    stencilFaceState.passOp      = WGPUStencilOperation_Keep;
}

void Application::SetDefaultDepthStencilState(WGPUDepthStencilState& depthStencilState)
{
    depthStencilState.nextInChain         = nullptr;
    depthStencilState.format              = WGPUTextureFormat_Undefined;
    depthStencilState.depthWriteEnabled   = WebGPUUtils::GenerateBool(false);
    depthStencilState.depthCompare        = WGPUCompareFunction_Always;
    depthStencilState.stencilReadMask     = 0xFFFFFFFF;
    depthStencilState.stencilWriteMask    = 0xFFFFFFFF;
    depthStencilState.depthBias           = 0;
    depthStencilState.depthBiasSlopeScale = 0;
    depthStencilState.depthBiasClamp      = 0;
    SetDefaultStencilFaceState(depthStencilState.stencilFront);
    SetDefaultStencilFaceState(depthStencilState.stencilBack);
}

bool Application::InitializePipeline()
{
    // Load the shader module
    WGPUShaderModule shaderModule =
        ResourceManager::LoadShaderModule("resources/shader.wgsl", device);

    if (shaderModule == nullptr)
    {
        SDL_Log("Could not load shader!");
        exit(EXIT_FAILURE);
    }

    // Create the render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.nextInChain                  = nullptr;

    // Describe vertex pipeline
    WGPUVertexBufferLayout vertexBufferLayout {};
    std::vector<WGPUVertexAttribute> vertexAttribs(4);
    vertexAttribs[0].shaderLocation = 0;  // Describe the position attribute
    vertexAttribs[0].format         = WGPUVertexFormat_Float32x3;
    vertexAttribs[0].offset         = offsetof(VertexAttributes, position);
    vertexAttribs[1].shaderLocation = 1;  // Describe the normal attribute
    vertexAttribs[1].format         = WGPUVertexFormat_Float32x3;
    vertexAttribs[1].offset         = offsetof(VertexAttributes, normal);
    vertexAttribs[2].shaderLocation = 2;  // Describe the color attribute
    vertexAttribs[2].format         = WGPUVertexFormat_Float32x3;
    vertexAttribs[2].offset         = offsetof(VertexAttributes, color);
    vertexAttribs[3].shaderLocation = 3;  // Describe the uv attribute
    vertexAttribs[3].format         = WGPUVertexFormat_Float32x2;
    vertexAttribs[3].offset         = offsetof(VertexAttributes, uv);

    vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
    vertexBufferLayout.attributes     = vertexAttribs.data();
    vertexBufferLayout.arrayStride    = sizeof(VertexAttributes);
    vertexBufferLayout.stepMode       = WGPUVertexStepMode_Vertex;

    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers     = &vertexBufferLayout;

    pipelineDesc.vertex.module        = shaderModule;
    pipelineDesc.vertex.entryPoint    = WebGPUUtils::GenerateString("vs_main");
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants     = nullptr;

    // Describe primitive pipeline
    pipelineDesc.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace        = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode         = WGPUCullMode_None;

    // Describe fragment pipeline
    WGPUFragmentState fragmentState {};
    fragmentState.module        = shaderModule;
    fragmentState.entryPoint    = WebGPUUtils::GenerateString("fs_main");
    fragmentState.constantCount = 0;
    fragmentState.constants     = nullptr;

    // blend settings
    WGPUBlendState blendState {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_Zero;
    blendState.alpha.dstFactor = WGPUBlendFactor_One;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget {};
    colorTarget.format    = surfaceFormat;
    colorTarget.blend     = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    fragmentState.targetCount = 1;
    fragmentState.targets     = &colorTarget;
    pipelineDesc.fragment     = &fragmentState;

    // Describe stencil/depth pipeline
    WGPUDepthStencilState depthStencilState;
    SetDefaultDepthStencilState(depthStencilState);
    depthStencilState.depthCompare      = WGPUCompareFunction_Less;
    depthStencilState.depthWriteEnabled = WebGPUUtils::GenerateBool(true);
    depthStencilState.format            = depthTextureFormat;
    depthStencilState.stencilReadMask   = 0;
    depthStencilState.stencilWriteMask  = 0;
    pipelineDesc.depthStencil           = &depthStencilState;

    // Describe multi-sampling pipeline
    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Describe pipeline layout
    // Create binding layouts
    std::vector<WGPUBindGroupLayoutEntry> bindingLayoutEntries(3);

    // for uniform buffer
    WGPUBindGroupLayoutEntry& bindingLayout = bindingLayoutEntries[0];
    SetDefaultBindGroupLayout(bindingLayout);
    bindingLayout.binding = 0;  // The binding index as used in the @binding attribute in the shader
    bindingLayout.visibility            = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bindingLayout.buffer.type           = WGPUBufferBindingType_Uniform;
    bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

    // for texture binding
    WGPUBindGroupLayoutEntry& textureBindingLayout = bindingLayoutEntries[1];
    SetDefaultBindGroupLayout(textureBindingLayout);
    textureBindingLayout.binding               = 1;
    textureBindingLayout.visibility            = WGPUShaderStage_Fragment;
    textureBindingLayout.texture.sampleType    = WGPUTextureSampleType_Float;
    textureBindingLayout.texture.viewDimension = WGPUTextureViewDimension_2D;

    // for texture sampler binding
    WGPUBindGroupLayoutEntry& samplerBindingLayout = bindingLayoutEntries[2];
    SetDefaultBindGroupLayout(samplerBindingLayout);
    samplerBindingLayout.binding      = 2;
    samplerBindingLayout.visibility   = WGPUShaderStage_Fragment;
    samplerBindingLayout.sampler.type = WGPUSamplerBindingType_Filtering;

    // Create a bind group layout
    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc {};
    bindGroupLayoutDesc.nextInChain = nullptr;
    bindGroupLayoutDesc.entryCount  = static_cast<uint32_t>(bindingLayoutEntries.size());
    bindGroupLayoutDesc.entries     = bindingLayoutEntries.data();
    bindGroupLayout                 = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc {};
    layoutDesc.nextInChain          = nullptr;
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts     = &bindGroupLayout;
    layout                          = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
    pipelineDesc.layout             = layout;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);

    return true;
}

void Application::TerminatePipeline()
{
    wgpuBindGroupLayoutRelease(bindGroupLayout);
    wgpuRenderPipelineRelease(pipeline);
    wgpuPipelineLayoutRelease(layout);
}

bool Application::InitializeTexture()
{
    // Create a sampler
    WGPUSamplerDescriptor samplerDesc;
    samplerDesc.nextInChain   = nullptr;
    samplerDesc.label         = WebGPUUtils::GenerateString("Sampler");
    samplerDesc.addressModeU  = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV  = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW  = WGPUAddressMode_Repeat;
    samplerDesc.magFilter     = WGPUFilterMode_Linear;
    samplerDesc.minFilter     = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter  = WGPUMipmapFilterMode_Linear;
    samplerDesc.lodMinClamp   = 0.0f;
    samplerDesc.lodMaxClamp   = 8.0f;
    samplerDesc.compare       = WGPUCompareFunction_Undefined;
    samplerDesc.maxAnisotropy = 1;
    sampler                   = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create texture
    texture =
        ResourceManager::LoadTexture("resources/fourareen2K_albedo.jpg", device, &textureView);

    if (!texture)
    {
        SDL_Log("Could not load texture!");
        return false;
    }

    return true;
}

void Application::TerminateTexture()
{
    wgpuTextureViewRelease(textureView);
    wgpuTextureDestroy(texture);
    wgpuTextureRelease(texture);
    wgpuSamplerRelease(sampler);
}

bool Application::InitializeGeometry()
{
    // Load mesh data from OBJ file
    std::vector<VertexAttributes> vertexData;
    bool success = ResourceManager::LoadGeometryFromObj("resources/fourareen.obj", vertexData);
    if (!success)
    {
        SDL_Log("Could not load geometry!");
        return false;
    }

    indexCount = static_cast<uint32_t>(vertexData.size());

    // Create vertex buffer
    WGPUBufferDescriptor bufferDesc {};
    bufferDesc.nextInChain      = nullptr;
    bufferDesc.size             = vertexData.size() * sizeof(VertexAttributes);
    bufferDesc.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    bufferDesc.mappedAtCreation = false;
    vertexBuffer                = wgpuDeviceCreateBuffer(device, &bufferDesc);

    wgpuQueueWriteBuffer(queue, vertexBuffer, 0, vertexData.data(), bufferDesc.size);

    return true;
}

void Application::TerminateGeometry()
{
    wgpuBufferRelease(vertexBuffer);
}

bool Application::InitializeUniforms()
{
    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc {};
    bufferDesc.nextInChain      = nullptr;
    bufferDesc.size             = sizeof(MyUniforms);
    bufferDesc.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    bufferDesc.mappedAtCreation = false;
    uniformBuffer               = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Upload the initial value of the uniforms
    uniforms.modelMatrix = glm::mat4x4(1.0);
    uniforms.viewMatrix =
        glm::lookAt(glm::vec3(-2.0f, -3.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    uniforms.projectionMatrix = glm::perspective(45 * PI / 180, 640.0f / 480.0f, 0.01f, 100.0f);
    uniforms.time             = 1.0f;
    uniforms.color            = {0.0f, 1.0f, 0.4f, 1.0f};

    wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &uniforms, sizeof(MyUniforms));

    return true;
}

void Application::TerminateUniforms()
{
    wgpuBufferRelease(uniformBuffer);
}

bool Application::InitializeBindGroups()
{
    // Create a binding
    std::vector<WGPUBindGroupEntry> bindings(3);

    bindings[0].nextInChain = nullptr;
    bindings[0].binding     = 0;
    bindings[0].buffer      = uniformBuffer;
    bindings[0].offset      = 0;
    bindings[0].size        = sizeof(MyUniforms);

    bindings[1].nextInChain = nullptr;
    bindings[1].binding     = 1;
    bindings[1].textureView = textureView;

    bindings[2].nextInChain = nullptr;
    bindings[2].binding     = 2;
    bindings[2].sampler     = sampler;

    // A bind group contains one or multiple bindings
    WGPUBindGroupDescriptor bindGroupDesc;
    bindGroupDesc.nextInChain = nullptr;
    bindGroupDesc.label       = WebGPUUtils::GenerateString("Bind Group");
    bindGroupDesc.layout      = bindGroupLayout;
    bindGroupDesc.entryCount  = static_cast<uint32_t>(bindings.size());
    bindGroupDesc.entries     = bindings.data();
    bindGroup                 = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

    return true;
}

void Application::TerminateBindGroups()
{
    wgpuBindGroupRelease(bindGroup);
}

WGPUTextureView Application::GetNextSurfaceTextureView()
{
    // Get the surface texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
    {
        return nullptr;
    }

    // Create a view for this surface texture
    WGPUTextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label       = WebGPUUtils::GenerateString("Surface texture view");
#ifndef __EMSCRIPTEN__
    viewDescriptor.usage = WGPUTextureUsage_RenderAttachment;
#endif
    viewDescriptor.format          = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension       = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel    = 0;
    viewDescriptor.mipLevelCount   = 1;
    viewDescriptor.baseArrayLayer  = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect          = WGPUTextureAspect_All;

    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

    wgpuTextureRelease(surfaceTexture.texture);

    return targetView;
}

void Application::UpdateViewMatrix()
{
    float cx            = std::cos(cameraState.angles.x);
    float sx            = std::sin(cameraState.angles.x);
    float cy            = std::cos(cameraState.angles.y);
    float sy            = std::sin(cameraState.angles.y);
    glm::vec3 position  = glm::vec3(cx * cy, sx * cy, sy) * std::exp(-cameraState.zoom);
    uniforms.viewMatrix = glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0, 0, 1));
    wgpuQueueWriteBuffer(queue,
                         uniformBuffer,
                         offsetof(MyUniforms, viewMatrix),
                         &uniforms.viewMatrix,
                         sizeof(MyUniforms::viewMatrix));
}

void Application::OnMouseMove()
{
    if (!dragState.active)
    {
        return;
    }

    SDL_SetRelativeMouseMode(SDL_FALSE);

    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);

    glm::vec2 currentMouse = glm::vec2(-(float)x, (float)y);
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
    static constexpr int LEFT_BUTTON = 1;
    if (!SDL_BUTTON(LEFT_BUTTON))
    {
        return;
    }

    bool isPressed = event.type == SDL_MOUSEBUTTONDOWN ? true : false;
    if (isPressed)
    {
        SDL_SetRelativeMouseMode(SDL_TRUE);

        dragState.active = true;
        int x = 0, y = 0;
        SDL_GetRelativeMouseState(&x, &y);
        dragState.startMouse       = glm::vec2(-(float)x, (float)y);
        dragState.startCameraState = cameraState;
    }
    else
    {
        dragState.active = false;
    }
}

void Application::OnScroll(SDL_Event& event)
{
    assert(event.type == SDL_MOUSEWHEEL);

    cameraState.zoom += dragState.scrollSensitivity * static_cast<float>(event.wheel.y);
    cameraState.zoom = glm::clamp(cameraState.zoom, -2.0f, 2.0f);
    UpdateViewMatrix();
}

void Application::SetDefaultLimits(WGPULimits& limits) const
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
    limits.maxUniformBufferBindingSize               = WGPU_LIMIT_U64_UNDEFINED;
    limits.maxStorageBufferBindingSize               = WGPU_LIMIT_U64_UNDEFINED;
    limits.minUniformBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
    limits.minStorageBufferOffsetAlignment           = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxVertexBuffers                          = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBufferSize                             = WGPU_LIMIT_U64_UNDEFINED;
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
