#include "Application.h"

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "WebGPUUtils.h"
#include "sdl2webgpu.h"

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

    // Configure the surface
    WGPUSurfaceConfiguration config = {};
    config.nextInChain              = nullptr;
    config.width                    = 1024;
    config.height                   = 768;
    config.usage                    = WGPUTextureUsage_RenderAttachment;
    config.format                   = WebGPUUtils::GetTextureFormat(surface, adapter);
    config.viewFormatCount          = 0;
    config.viewFormats              = nullptr;
    config.device                   = device;
    config.presentMode              = WGPUPresentMode_Fifo;
    config.alphaMode                = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(surface, &config);

    wgpuAdapterRelease(adapter);

    return true;
}

void Application::Terminate()
{
    // Terminate WebGPU
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
    renderPassColorAttachment.clearValue                    = WGPUColor {0.9, 0.1, 0.2, 1.0};
    renderPassColorAttachment.depthSlice                    = WGPU_DEPTH_SLICE_UNDEFINED;

    renderPassDesc.colorAttachmentCount   = 1;
    renderPassDesc.colorAttachments       = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites        = nullptr;

    // Create the render pass and end it immediately (we only clear the screen but do not draw anything)
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
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
