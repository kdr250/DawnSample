#include "WebGPUUtils.h"

#include <SDL3/SDL_log.h>
#include <cassert>
#include <vector>

/**
 * See:
 * https://developer.chrome.com/docs/web-platform/webgpu/build-app?hl=ja#get_gpu_device
 */
wgpu::Adapter WebGPUUtils::RequestAdapterSync(wgpu::Instance instance,
                                              wgpu::RequestAdapterOptions const* options)
{
    wgpu::Adapter result = nullptr;

    wgpu::Future future =
        instance.RequestAdapter(options,
                                wgpu::CallbackMode::WaitAnyOnly,
                                [&result](wgpu::RequestAdapterStatus status,
                                          wgpu::Adapter adapter,
                                          wgpu::StringView message)
                                {
                                    if (status != wgpu::RequestAdapterStatus::Success)
                                    {
                                        printf("Could not get WebGPU adapter: %s\n", message.data);
                                        exit(1);
                                    }
                                    result = std::move(adapter);
                                });
    instance.WaitAny(future, UINT64_MAX);

    return result;
}

/**
 * See:
 * https://developer.chrome.com/docs/web-platform/webgpu/build-app?hl=ja#get_gpu_device
 */
wgpu::Device WebGPUUtils::RequestDeviceSync(wgpu::Instance instance,
                                            wgpu::Adapter adapter,
                                            wgpu::DeviceDescriptor const* descripter)
{
    wgpu::Device result = nullptr;

    wgpu::Future future = adapter.RequestDevice(
        descripter,
        wgpu::CallbackMode::AllowSpontaneous,
        [&result](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message)
        {
            if (status != wgpu::RequestDeviceStatus::Success)
            {
                printf("Could not get WebGPU device: %s\n", message.data);
                exit(1);
            }
            result = std::move(device);
        });

    instance.WaitAny(future, UINT64_MAX);

    return result;
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
