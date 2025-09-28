#include "WebGPUUtils.h"

#include <SDL2/SDL_log.h>
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

#ifdef __EMSCRIPTEN__
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status,
                                    WGPUAdapter adapter,
                                    const char* message,
                                    void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestAdapterStatus_Success)
        {
            userData.adapter = wgpu::Adapter::Acquire(adapter);
        }
        else
        {
            SDL_Log("Could not get WebGPU adapter: %s", message);
        }
        userData.requestEnded = true;
    };

    instance.RequestAdapter(options, onAdapterRequestEnded, (void*)&userData);
#else
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
#endif

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

#ifdef __EMSCRIPTEN__
    auto onDeviceRequestEnded =
        [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestDeviceStatus_Success)
        {
            userData.device = wgpu::Device::Acquire(device);
        }
        else
        {
            SDL_Log("Could not get WebGPU device: %s", message);
        }
        userData.requestEnded = true;
    };

    adapter.RequestDevice(descripter, onDeviceRequestEnded, (void*)&userData);
#else
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
#endif

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
#ifdef __EMSCRIPTEN__
    std::vector<wgpu::FeatureName> features;

    // Call the function a first time with a null return address, just to get
    // the entry count.
    size_t featureCount = adapter.EnumerateFeatures(nullptr);

    // Allocate memory (could be a new, or a malloc() if this were a C program)
    features.resize(featureCount);

    // Call the function a second time, with a non-null return address
    adapter.EnumerateFeatures(features.data());

    SDL_Log("Adapter features:");
    for (auto feature : features)
    {
        SDL_Log(" - 0x%08X", feature);
    }

    wgpu::AdapterProperties properties = {};
    properties.nextInChain             = nullptr;
    adapter.GetProperties(&properties);
    SDL_Log("Adapter properties:");
    SDL_Log(" - vendorID: %i", properties.vendorID);
    if (properties.vendorName)
    {
        SDL_Log(" - vendorName: %s", properties.vendorName);
    }
    if (properties.architecture)
    {
        SDL_Log(" - architecture: %s", properties.architecture);
    }
    SDL_Log(" - deviceID: %i", properties.deviceID);
    if (properties.name)
    {
        SDL_Log(" - name: %s", properties.name);
    }
    if (properties.driverDescription)
    {
        SDL_Log(" - driverDescription: %s", properties.driverDescription);
    }
    SDL_Log(" - adapterType: 0x%08X", properties.adapterType);
    SDL_Log(" - backendType: 0x%08X", properties.backendType);
#else
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
#endif
}

void WebGPUUtils::InspectDevice(wgpu::Device device)
{
#ifdef __EMSCRIPTEN__
    std::vector<wgpu::FeatureName> features;
    size_t featureCount = device.EnumerateFeatures(nullptr);
    features.resize(featureCount);
    device.EnumerateFeatures(features.data());

    SDL_Log("Device features:");
    for (auto feature : features)
    {
        SDL_Log(" - 0x%08X", feature);
    }

    wgpu::SupportedLimits supportedLimits = {};
    supportedLimits.nextInChain           = nullptr;

    bool success = device.GetLimits(&supportedLimits);

    if (success)
    {
        SDL_Log("Device limits:");
        SDL_Log(" - maxTextureDimension1D: %d", supportedLimits.limits.maxTextureDimension1D);
        SDL_Log(" - maxTextureDimension2D: %d", supportedLimits.limits.maxTextureDimension2D);
        SDL_Log(" - maxTextureDimension3D: %d", supportedLimits.limits.maxTextureDimension3D);
        SDL_Log(" - maxTextureArrayLayers: %d", supportedLimits.limits.maxTextureArrayLayers);
    }
#else
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
#endif
}

wgpu::TextureFormat WebGPUUtils::GetTextureFormat(wgpu::Surface surface, wgpu::Adapter adapter)
{
#ifdef __EMSCRIPTEN__
    wgpu::TextureFormat surfaceFormat = surface.GetPreferredFormat(adapter);
#else
    wgpu::SurfaceCapabilities capabilities;
    wgpu::Status status = surface.GetCapabilities(adapter, &capabilities);
    if (status != wgpu::Status::Success)
    {
        SDL_Log("Could not get surface capabilities! return wgpu::TextureFormat_Undefined");
        return wgpu::TextureFormat::Undefined;
    }
    wgpu::TextureFormat surfaceFormat = capabilities.formats[0];
#endif

    return surfaceFormat;
}
