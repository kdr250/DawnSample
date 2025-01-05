#include "Application.h"

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "WebGPUUtils.h"
#include "sdl2webgpu.h"

// We embbed the source of the shader module here
const char* shaderSource = R"(

struct Output {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
};

@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> Output {
    var output: Output;
    if (in_vertex_index == 0u) {
        output.position = vec4f(-0.5, -0.5, 0.0, 1.0);
        output.color = vec3f(1.0, 0.0, 0.0);
    } else if (in_vertex_index == 1u) {
        output.position = vec4f(0.5, -0.5, 0.0, 1.0);
        output.color = vec3f(0.0, 1.0, 0.0);
    } else {
        output.position = vec4f(0.0, 0.5, 0.0, 1.0);
        output.color = vec3f(0.0, 0.0, 1.0);
    }
    return output;
}

@fragment
fn fs_main(input: Output) -> @location(0) vec4f {
    return vec4f(input.color, 1.0);
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
    deviceDesc.requiredLimits           = nullptr;
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

    return true;
}

void Application::Terminate()
{
    // Terminate WebGPU
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
    renderPassColorAttachment.clearValue                    = WGPUColor {0.0, 0.0, 0.0, 1.0};
    renderPassColorAttachment.depthSlice                    = WGPU_DEPTH_SLICE_UNDEFINED;

    renderPassDesc.colorAttachmentCount   = 1;
    renderPassDesc.colorAttachments       = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites        = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // set pipeline to renderpass and draw
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);

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
#ifdef __EMSCRIPTEN__
    emscripten_sleep(100);
#else
    wgpuDeviceTick(device);
#endif
}

bool Application::IsRunning()
{
    return isRunning;
}

void Application::InitializePipeline()
{
    // Load the shader module
    WGPUShaderModuleDescriptor shaderDesc {};

    WGPUShaderModuleWGSLDescriptor shaderCodeDesc {};
    shaderCodeDesc.chain.next = nullptr;
#ifdef __EMSCRIPTEN__
    shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
#else
    shaderCodeDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
#endif
    // conect the chain
    shaderDesc.nextInChain        = &shaderCodeDesc.chain;
    shaderCodeDesc.code           = WebGPUUtils::GenerateString(shaderSource);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create the render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.nextInChain                  = nullptr;

    // Describe vertex pipeline
    pipelineDesc.vertex.bufferCount   = 0;
    pipelineDesc.vertex.buffers       = nullptr;
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
    pipelineDesc.depthStencil = nullptr;

    // Describe multi-sampling pipeline
    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Describe pipeline layout
    pipelineDesc.layout = nullptr;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
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
