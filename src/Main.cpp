#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "WebGPUUtils.h"
#include "sdl2webgpu.h"

SDL_Window* window;
WGPUInstance instance;
WGPUAdapter adapter;
WGPUDevice device;
WGPUQueue queue;
WGPUSurface surface;
bool isRunning = true;

void Quit()
{
    // Terminate WebGPU
    wgpuSurfaceRelease(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);

    // Terminate SDL
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void MainLoop()
{
    if (!isRunning)
    {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop(); /* this should "kill" the app. */
        Quit();
        return;
#endif
    }

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
}

int main(int argc, char* argv[])
{
// Init WebGPU
#ifdef __EMSCRIPTEN__
    instance = wgpuCreateInstance(nullptr);
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

    instance = wgpuCreateInstance(&desc);
#endif
    if (instance == nullptr)
    {
        SDL_Log("Instance creation failed!");
        return EXIT_FAILURE;
    }

    // Init SDL
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Dawn Sample",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              1024,
                              768,
                              0);

    // Requesting Adapter
    WGPURequestAdapterOptions adapterOptions = {};
    adapterOptions.nextInChain               = nullptr;
    adapter = WebGPUUtils::RequestAdapterSync(instance, &adapterOptions);

    WebGPUUtils::InspectAdapter(adapter);

    wgpuInstanceRelease(instance);

    // Requesting Device
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain          = nullptr;
#ifdef __EMSCRIPTEN__
    deviceDesc.label                    = "My Device";
    deviceDesc.requiredFeatureCount     = 0;
    deviceDesc.requiredLimits           = nullptr;
    deviceDesc.defaultQueue.nextInChain = nullptr;
    deviceDesc.defaultQueue.label       = "The default queue";
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
    deviceDesc.label                               = {"My Device", WGPU_STRLEN};
    deviceDesc.requiredFeatureCount                = 0;
    deviceDesc.requiredLimits                      = nullptr;
    deviceDesc.defaultQueue.nextInChain            = nullptr;
    deviceDesc.defaultQueue.label                  = {"The default queue", WGPU_STRLEN};
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
#else
    auto onDeviceError = [](WGPULoggingType type, WGPUStringView message, void* /* pUserData */)
    {
        SDL_Log("device logging: type 0x%08X", type);
        if (message.data)
        {
            SDL_Log(" - message: %s", message.data);
        }
    };
    wgpuDeviceSetLoggingCallback(device, onDeviceError, nullptr);
#endif

    WebGPUUtils::InspectDevice(device);

    wgpuAdapterRelease(adapter);

    queue = wgpuDeviceGetQueue(device);

    // Create WebGPU surface
    surface = SDL_GetWGPUSurface(instance, window);

    // Queue
    auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /* pUserData */)
    {
        SDL_Log("Queue work finished with status: 0x%08X", status);
    };
    wgpuQueueOnSubmittedWorkDone(queue, onQueueWorkDone, nullptr);

    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain                  = nullptr;
#ifdef __EMSCRIPTEN__
    encoderDesc.label = "My command encoder";
#else
    encoderDesc.label = {"My command encoder", WGPU_STRLEN};
#endif
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

#ifdef __EMSCRIPTEN__
    wgpuCommandEncoderInsertDebugMarker(encoder, "Do one thing");
    wgpuCommandEncoderInsertDebugMarker(encoder, "Do another thing");
#else
    wgpuCommandEncoderInsertDebugMarker(encoder, {"Do one thing", WGPU_STRLEN});
    wgpuCommandEncoderInsertDebugMarker(encoder, {"Do another thing", WGPU_STRLEN});
#endif

    WGPUCommandBufferDescriptor cmdBufferDescripter = {};
    cmdBufferDescripter.nextInChain                 = nullptr;
#ifdef __EMSCRIPTEN__
    cmdBufferDescripter.label = "Command buffer";
#else
    cmdBufferDescripter.label = {"Command buffer", WGPU_STRLEN};
#endif

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescripter);

    wgpuCommandEncoderRelease(encoder);

    // Submit the command queue
    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);

    for (int i = 0; i < 5; ++i)
    {
        SDL_Log("Tick/Poll device...");
#ifdef __EMSCRIPTEN__
        emscripten_sleep(100);
#else
        wgpuDeviceTick(device);
#endif
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(MainLoop, 0, 1);
#else
    while (isRunning)
    {
        MainLoop();
    }
    Quit();
#endif

    return EXIT_SUCCESS;
}
