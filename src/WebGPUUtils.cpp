#include "WebGPUUtils.h"

#include <SDL2/SDL_log.h>
#include <cassert>
#include <vector>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

WGPUAdapter WebGPUUtils::RequestAdapterSync(WGPUInstance instance,
                                            WGPURequestAdapterOptions const* options)
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

WGPUDevice WebGPUUtils::RequestDeviceSync(WGPUAdapter adapter,
                                          WGPUDeviceDescriptor const* descripter)
{
    struct UserData
    {
        WGPUDevice device = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

#ifdef __EMSCRIPTEN__
    auto onDeviceRequestEnded =
        [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestDeviceStatus_Success)
        {
            userData.device = device;
        }
        else
        {
            SDL_Log("Could not get WebGPU device: %s", message);
        }
        userData.requestEnded = true;
    };
#else
    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status,
                                   WGPUDevice device,
                                   WGPUStringView message,
                                   void* pUserData)
    {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestDeviceStatus_Success)
        {
            userData.device = device;
        }
        else
        {
            SDL_Log("Could not get WebGPU device: %s", message.data);
        }
        userData.requestEnded = true;
    };
#endif

    wgpuAdapterRequestDevice(adapter, descripter, onDeviceRequestEnded, (void*)&userData);

#ifdef __EMSCRIPTEN__
    while (!userData.requestEnded)
    {
        emscripten_sleep(100);
    }
#endif

    assert(userData.requestEnded);

    return userData.device;
}

void WebGPUUtils::InspectAdapter(WGPUAdapter adapter)
{
#ifdef __EMSCRIPTEN__
    std::vector<WGPUFeatureName> features;

    // Call the function a first time with a null return address, just to get
    // the entry count.
    size_t featureCount = wgpuAdapterEnumerateFeatures(adapter, nullptr);

    // Allocate memory (could be a new, or a malloc() if this were a C program)
    features.resize(featureCount);

    // Call the function a second time, with a non-null return address
    wgpuAdapterEnumerateFeatures(adapter, features.data());

    SDL_Log("Adapter features:");
    for (auto feature : features)
    {
        SDL_Log(" - 0x%08X", feature);
    }

    WGPUAdapterProperties properties = {};
    properties.nextInChain           = nullptr;
    wgpuAdapterGetProperties(adapter, &properties);
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

void WebGPUUtils::InspectDevice(WGPUDevice device)
{
#ifdef __EMSCRIPTEN__
    std::vector<WGPUFeatureName> features;
    size_t featureCount = wgpuDeviceEnumerateFeatures(device, nullptr);
    features.resize(featureCount);
    wgpuDeviceEnumerateFeatures(device, features.data());

    SDL_Log("Device features:");
    for (auto feature : features)
    {
        SDL_Log(" - 0x%08X", feature);
    }

    WGPUSupportedLimits supportedLimits = {};
    supportedLimits.nextInChain         = nullptr;

    bool success = wgpuDeviceGetLimits(device, &supportedLimits);

    if (success)
    {
        SDL_Log("Device limits:");
        SDL_Log(" - maxTextureDimension1D: %d", supportedLimits.limits.maxTextureDimension1D);
        SDL_Log(" - maxTextureDimension2D: %d", supportedLimits.limits.maxTextureDimension2D);
        SDL_Log(" - maxTextureDimension3D: %d", supportedLimits.limits.maxTextureDimension3D);
        SDL_Log(" - maxTextureArrayLayers: %d", supportedLimits.limits.maxTextureArrayLayers);
    }
#else
    WGPUSupportedFeatures features;
    wgpuDeviceGetFeatures(device, &features);

    SDL_Log("Device features:");
    for (int i = 0; i < features.featureCount; ++i)
    {
        auto feature = features.features[i];
        SDL_Log(" - 0x%08X", feature);
    }

    WGPUSupportedLimits supportedLimits = {};
    supportedLimits.nextInChain         = nullptr;
    WGPUStatus status                   = wgpuDeviceGetLimits(device, &supportedLimits);

    if (status == WGPUStatus_Success)
    {
        SDL_Log("Device limits:");
        SDL_Log(" - maxTextureDimension1D: %d", supportedLimits.limits.maxTextureDimension1D);
        SDL_Log(" - maxTextureDimension2D: %d", supportedLimits.limits.maxTextureDimension2D);
        SDL_Log(" - maxTextureDimension3D: %d", supportedLimits.limits.maxTextureDimension3D);
        SDL_Log(" - maxTextureArrayLayers: %d", supportedLimits.limits.maxTextureArrayLayers);
    }
#endif
}
