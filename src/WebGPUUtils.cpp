#include "WebGPUUtils.h"

#include <SDL3/SDL_log.h>
#include <cassert>
#include <vector>

wgpu::Adapter WebGPUUtils::RequestAdapterSync(wgpu::Instance instance,
                                              wgpu::RequestAdapterOptions const* options)
{
    struct UserData
    {
        wgpu::Adapter adapter = nullptr;
        bool requestEnded     = false;
    };
    UserData userData;

    auto onAdapterRequestEnded = [](wgpu::RequestAdapterStatus status,
                                    wgpu::Adapter adapter,
                                    wgpu::StringView message,
                                    UserData* pUserData)
    {
        if (status == wgpu::RequestAdapterStatus::Success)
        {
            pUserData->adapter = adapter;
        }
        else
        {
            SDL_Log("Could not get WebGPU adapter: %s", message.data);
        }
        pUserData->requestEnded = true;
    };

    instance.RequestAdapter(options,
                            wgpu::CallbackMode::AllowSpontaneous,
                            onAdapterRequestEnded,
                            &userData);

#ifdef __EMSCRIPTEN__
    while (!userData.requestEnded)
    {
        emscripten_sleep(100);
    }
#endif

    assert(userData.requestEnded);

    return userData.adapter;
}

wgpu::Device WebGPUUtils::RequestDeviceSync(wgpu::Adapter adapter,
                                            wgpu::DeviceDescriptor const* descripter)
{
    struct UserData
    {
        wgpu::Device device = nullptr;
        bool requestEnded   = false;
    };
    UserData userData;

    auto onDeviceRequestEnded = [](wgpu::RequestDeviceStatus status,
                                   wgpu::Device device,
                                   wgpu::StringView message,
                                   UserData* userData)
    {
        if (status == wgpu::RequestDeviceStatus::Success)
        {
            userData->device = device;
        }
        else
        {
            SDL_Log("Could not get WebGPU device: %s", message.data);
        }
        userData->requestEnded = true;
    };

    adapter.RequestDevice(descripter,
                          wgpu::CallbackMode::AllowSpontaneous,
                          onDeviceRequestEnded,
                          &userData);

#ifdef __EMSCRIPTEN__
    while (!userData.requestEnded)
    {
        emscripten_sleep(100);
    }
#endif

    assert(userData.requestEnded);

    return userData.device;
}

void WebGPUUtils::InspectAdapter(wgpu::Adapter adapter)
{
    wgpu::Limits supportedLimits = {};
    supportedLimits.nextInChain  = nullptr;

    wgpu::Status status = adapter.GetLimits(&supportedLimits);
    if (status == wgpu::Status::Success)
    {
        SDL_Log("Adapter limits:");
        SDL_Log(" - maxTextureDimension1D: %d", supportedLimits.maxTextureDimension1D);
        SDL_Log(" - maxTextureDimension2D: %d", supportedLimits.maxTextureDimension2D);
        SDL_Log(" - maxTextureDimension3D: %d", supportedLimits.maxTextureDimension3D);
        SDL_Log(" - maxTextureArrayLayers: %d", supportedLimits.maxTextureArrayLayers);
    }

    wgpu::SupportedFeatures features;
    adapter.GetFeatures(&features);

    SDL_Log("Adapter features:");
    for (int i = 0; i < features.featureCount; ++i)
    {
        auto feature = features.features[i];
        SDL_Log(" - 0x%08X", feature);
    }

    wgpu::AdapterInfo info;
    info.nextInChain = nullptr;
    adapter.GetInfo(&info);

    SDL_Log("Adapter properties:");
    SDL_Log(" - vendorID: %i", info.vendorID);
    if (info.vendor.data)
    {
        SDL_Log(" - vendor: %s", info.vendor.data);
    }
    if (info.architecture.data)
    {
        SDL_Log(" - architecture: %s", info.architecture.data);
    }
    SDL_Log(" - deviceID: %i", info.deviceID);
    if (info.device.data)
    {
        SDL_Log(" - device: %s", info.device.data);
    }
    if (info.description.data)
    {
        SDL_Log(" - description: %s", info.description.data);
    }
    SDL_Log(" - adapterType: 0x%08X", info.adapterType);
    SDL_Log(" - backendType: 0x%08X", info.backendType);
}

void WebGPUUtils::InspectDevice(wgpu::Device device)
{
    wgpu::SupportedFeatures features;
    device.GetFeatures(&features);

    SDL_Log("Device features:");
    for (int i = 0; i < features.featureCount; ++i)
    {
        auto feature = features.features[i];
        SDL_Log(" - 0x%08X", feature);
    }

    wgpu::Limits supportedLimits = {};
    supportedLimits.nextInChain  = nullptr;
    wgpu::Status status          = device.GetLimits(&supportedLimits);

    if (status == wgpu::Status::Success)
    {
        SDL_Log("Device limits:");

        SDL_Log(" - maxTextureDimension1D: %d", supportedLimits.maxTextureDimension1D);
        SDL_Log(" - maxTextureDimension2D: %d", supportedLimits.maxTextureDimension2D);
        SDL_Log(" - maxTextureDimension3D: %d", supportedLimits.maxTextureDimension3D);
        SDL_Log(" - maxTextureArrayLayers: %d", supportedLimits.maxTextureArrayLayers);
    }
}

wgpu::TextureFormat WebGPUUtils::GetTextureFormat(wgpu::Surface surface, wgpu::Adapter adapter)
{
    wgpu::SurfaceCapabilities capabilities;
    wgpu::Status status = surface.GetCapabilities(adapter, &capabilities);
    if (status != wgpu::Status::Success)
    {
        SDL_Log("Could not get surface capabilities! return wgpu::TextureFormat_Undefined");
        return wgpu::TextureFormat::Undefined;
    }
    wgpu::TextureFormat surfaceFormat = capabilities.formats[0];

    return surfaceFormat;
}
