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

    InitializePipeline();
    InitializeBuffers();
    InitializeBindGroups();

    return true;
}

void Application::Terminate()
{
    // Terminate WebGPU
    wgpuTextureViewRelease(textureView);
    wgpuTextureDestroy(texture);
    wgpuTextureRelease(texture);
    wgpuTextureViewRelease(depthTextureView);
    wgpuTextureDestroy(depthTexture);
    wgpuTextureRelease(depthTexture);
    wgpuBindGroupRelease(bindGroup);
    wgpuPipelineLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(bindGroupLayout);
    wgpuBufferRelease(uniformBuffer);
    wgpuBufferRelease(vertexBuffer);
    wgpuRenderPipelineRelease(pipeline);
    wgpuSurfaceUnconfigure(surface);
    wgpuQueueRelease(queue);
    wgpuSurfaceRelease(surface);
    wgpuDeviceRelease(device);

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

    // Update uniform buffer
    uniforms.time = SDL_GetTicks64() / 1000.0f;
    wgpuQueueWriteBuffer(queue,
                         uniformBuffer,
                         offsetof(MyUniforms, time),
                         &uniforms.time,
                         sizeof(MyUniforms::time));

    float viewZ = glm::mix(0.0f, 0.25f, cos(2 * PI * uniforms.time / 4) * 0.5 + 0.5);
    uniforms.viewMatrix =
        glm::lookAt(glm::vec3(-0.5f, -1.5f, viewZ + 0.25f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    wgpuQueueWriteBuffer(queue,
                         uniformBuffer,
                         offsetof(MyUniforms, viewMatrix),
                         &uniforms.viewMatrix,
                         sizeof(MyUniforms::viewMatrix));

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
    requiredLimits.limits.maxBufferSize                 = 10000 * sizeof(VertexAttributes);
    requiredLimits.limits.maxVertexBufferArrayStride    = sizeof(VertexAttributes);
    requiredLimits.limits.maxInterStageShaderComponents = 8;

    // uniform
    requiredLimits.limits.maxBindGroups                   = 1;
    requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
    requiredLimits.limits.maxUniformBufferBindingSize     = 16 * 4 * sizeof(float);

    // for the depth buffer
    requiredLimits.limits.maxTextureDimension1D = 768;
    requiredLimits.limits.maxTextureDimension2D = 1024;
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

void Application::InitializePipeline()
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
    depthStencilState.depthCompare       = WGPUCompareFunction_Less;
    depthStencilState.depthWriteEnabled  = WebGPUUtils::GenerateBool(true);
    WGPUTextureFormat depthTextureFormat = WGPUTextureFormat_Depth24Plus;
    depthStencilState.format             = depthTextureFormat;
    depthStencilState.stencilReadMask    = 0;
    depthStencilState.stencilWriteMask   = 0;
    pipelineDesc.depthStencil            = &depthStencilState;

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

    // Create the color texture
    WGPUTextureDescriptor textureDesc;
    textureDesc.nextInChain     = nullptr;
    textureDesc.label           = WebGPUUtils::GenerateString("Texture");
    textureDesc.dimension       = WGPUTextureDimension_2D;
    textureDesc.size            = {256, 256, 1};
    textureDesc.mipLevelCount   = 8;
    textureDesc.sampleCount     = 1;
    textureDesc.format          = WGPUTextureFormat_RGBA8Unorm;
    textureDesc.usage           = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    textureDesc.viewFormatCount = 0;
    textureDesc.viewFormats     = nullptr;
    texture                     = wgpuDeviceCreateTexture(device, &textureDesc);

    WGPUTextureViewDescriptor textureViewDesc;
    textureViewDesc.nextInChain     = nullptr;
    textureViewDesc.label           = WebGPUUtils::GenerateString("Texture View");
    textureViewDesc.aspect          = WGPUTextureAspect_All;
    textureViewDesc.baseArrayLayer  = 0;
    textureViewDesc.arrayLayerCount = 1;
    textureViewDesc.baseMipLevel    = 0;
    textureViewDesc.mipLevelCount   = textureDesc.mipLevelCount;
    textureViewDesc.dimension       = WGPUTextureViewDimension_2D;
#ifndef __EMSCRIPTEN__
    textureViewDesc.usage = WGPUTextureUsage_None;
#endif
    textureViewDesc.format = textureDesc.format;
    textureView            = wgpuTextureCreateView(texture, &textureViewDesc);

    // Create a sampler
    WGPUSamplerDescriptor samplerDesc;
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

    // Create and upload texture data, one mip level at a time
    WGPUImageCopyTexture destination;
    destination.texture = texture;
    destination.origin  = {0, 0, 0};
    destination.aspect  = WGPUTextureAspect_All;

    WGPUTextureDataLayout source;
    source.offset = 0;

    WGPUExtent3D mipLevelSize = textureDesc.size;
    std::vector<uint8_t> previousLevelPixels;
    for (uint32_t level = 0; level < textureDesc.mipLevelCount; ++level)
    {
        // Create image data
        std::vector<uint8_t> pixels(4 * mipLevelSize.width * mipLevelSize.height);
        for (uint32_t i = 0; i < mipLevelSize.width; ++i)
        {
            for (uint32_t j = 0; j < mipLevelSize.height; ++j)
            {
                uint8_t* p = &pixels[4 * (j * mipLevelSize.width + i)];
                if (level == 0)
                {
                    p[0] = (i / 16) % 2 == (j / 16) % 2 ? 255 : 0;  // r
                    p[1] = ((i - j) / 16) % 2 == 0 ? 255 : 0;       // g
                    p[2] = ((i + j) / 16) % 2 == 0 ? 255 : 0;       // b
                }
                else
                {
                    // Get the corresponding 4 pixels from the previous level
                    uint8_t* p00 = &previousLevelPixels[4
                                                        * ((2 * j + 0) * (2 * mipLevelSize.width)
                                                           + (2 * i + 0))];
                    uint8_t* p01 = &previousLevelPixels[4
                                                        * ((2 * j + 0) * (2 * mipLevelSize.width)
                                                           + (2 * i + 1))];
                    uint8_t* p10 = &previousLevelPixels[4
                                                        * ((2 * j + 1) * (2 * mipLevelSize.width)
                                                           + (2 * i + 0))];
                    uint8_t* p11 = &previousLevelPixels[4
                                                        * ((2 * j + 1) * (2 * mipLevelSize.width)
                                                           + (2 * i + 1))];
                    // Average
                    p[0] = (p00[0] + p01[0] + p10[0] + p11[0]) / 4;
                    p[1] = (p00[1] + p01[1] + p10[1] + p11[1]) / 4;
                    p[2] = (p00[2] + p01[2] + p10[2] + p11[2]) / 4;
                }
                p[3] = 255;  // a
            }
        }

        // Change this to the current level
        destination.mipLevel = level;

        // Compute from the mip level size
        source.bytesPerRow  = 4 * mipLevelSize.width;
        source.rowsPerImage = mipLevelSize.height;

        wgpuQueueWriteTexture(queue,
                              &destination,
                              pixels.data(),
                              pixels.size(),
                              &source,
                              &mipLevelSize);

        // The size of the next mip level:
        mipLevelSize.width /= 2;
        mipLevelSize.height /= 2;
        previousLevelPixels = std::move(pixels);
    }

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

    wgpuQueueWriteTexture(queue,
                          &destination,
                          pixels.data(),
                          pixels.size(),
                          &source,
                          &textureDesc.size);

    wgpuShaderModuleRelease(shaderModule);
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

    indexCount = static_cast<uint32_t>(vertexData.size());

    // Create vertex buffer
    WGPUBufferDescriptor bufferDesc {};
    bufferDesc.nextInChain      = nullptr;
    bufferDesc.size             = vertexData.size() * sizeof(VertexAttributes);
    bufferDesc.usage            = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    bufferDesc.mappedAtCreation = false;
    vertexBuffer                = wgpuDeviceCreateBuffer(device, &bufferDesc);

    wgpuQueueWriteBuffer(queue, vertexBuffer, 0, vertexData.data(), bufferDesc.size);

    // Create uniform buffer
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
}

void Application::InitializeBindGroups()
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
