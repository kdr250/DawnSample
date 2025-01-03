#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>
#include <cassert>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "sdl2webgpu.h"

SDL_Window* window;
WGPUInstance instance;
WGPUAdapter adapter;
WGPUSurface surface;
bool isRunning = true;

WGPUAdapter RequestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const* options)
{
    struct UserData
    {
        WGPUAdapter adapter = nullptr;
        bool requestEnded   = false;
    };
    UserData userData;

#ifdef __EMSCRIPTEN__
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status,
                                    WGPUAdapter adapter,
                                    const char* message,
                                    void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestAdapterStatus_Success)
        {
            userData.adapter = adapter;
        }
        else
        {
            SDL_Log("Could not get WebGPU adapter: %s", message);
        }
        userData.requestEnded = true;
    };
#else
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status,
                                    WGPUAdapter adapter,
                                    WGPUStringView message,
                                    void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestAdapterStatus_Success)
        {
            userData.adapter = adapter;
        }
        else
        {
            SDL_Log("Could not get WebGPU adapter: %s", message.data);
        }
        userData.requestEnded = true;
    };
#endif

    wgpuInstanceRequestAdapter(instance, options, onAdapterRequestEnded, (void*)&userData);

#ifdef __EMSCRIPTEN__
    while (!userData.requestEnded)
    {
        emscripten_sleep(100);
    }
#endif

    assert(userData.requestEnded);

    return userData.adapter;
}

void InspectAdapter(WGPUAdapter adapter)
{
#ifndef __EMSCRIPTEN__
    WGPUSupportedLimits supportedLimits = {};
    supportedLimits.nextInChain         = nullptr;

    WGPUStatus status = wgpuAdapterGetLimits(adapter, &supportedLimits);
    if (status == WGPUStatus_Success)
    {
        SDL_Log("Adapter limits:");
        SDL_Log(" - maxTextureDimension1D: %d", supportedLimits.limits.maxTextureDimension1D);
        SDL_Log(" - maxTextureDimension2D: %d", supportedLimits.limits.maxTextureDimension2D);
        SDL_Log(" - maxTextureDimension3D: %d", supportedLimits.limits.maxTextureDimension3D);
        SDL_Log(" - maxTextureArrayLayers: %d", supportedLimits.limits.maxTextureArrayLayers);
    }

    WGPUSupportedFeatures features;
    wgpuAdapterGetFeatures(adapter, &features);

    SDL_Log("Adapter features:");
    for (int i = 0; i < features.featureCount; ++i)
    {
        auto feature = features.features[i];
        SDL_Log(" - 0x%08X", feature);
    }

    WGPUAdapterInfo info;
    info.nextInChain = nullptr;
    wgpuAdapterGetInfo(adapter, &info);

    SDL_Log("Adapter properties:");
    SDL_Log(" - vendorID: %i", info.vendorID);
    if (info.vendor.length > 0)
    {
        SDL_Log(" - vendor: %s", info.vendor.data);
    }
    if (info.architecture.length > 0)
    {
        SDL_Log(" - architecture: %s", info.architecture.data);
    }
    SDL_Log(" - deviceID: %i", info.deviceID);
    if (info.device.length > 0)
    {
        SDL_Log(" - device: %s", info.device.data);
    }
    if (info.description.length > 0)
    {
        SDL_Log(" - description: %s", info.description.data);
    }
    SDL_Log(" - adapterType: 0x%08X", info.adapterType);
    SDL_Log(" - backendType: 0x%08X", info.backendType);
#endif
}

void Quit()
{
    // Terminate WebGPU
    wgpuSurfaceRelease(surface);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

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
    instance = wgpuCreateInstance(nullptr);
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
    adapter                                  = RequestAdapterSync(instance, &adapterOptions);

    InspectAdapter(adapter);

    // Create WebGPU surface
    surface = SDL_GetWGPUSurface(instance, window);

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
