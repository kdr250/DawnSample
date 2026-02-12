#pragma once

#include <webgpu/webgpu_cpp.h>
#include <string>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

namespace WebGPUUtils
{
    /**
     * Utility function to get a WebGPU adapter
     */
    wgpu::Adapter RequestAdapterSync(wgpu::Instance instance,
                                     wgpu::RequestAdapterOptions const* options);

    /**
     * Utility function to get a WebGPU device
     */
    wgpu::Device RequestDeviceSync(wgpu::Instance instance,
                                   wgpu::Adapter adapter,
                                   wgpu::DeviceDescriptor const* descripter);

    /**
     * An example of how we can inspect the capabilities of the hardware through
     * the adapter object.
     */
    void InspectAdapter(wgpu::Adapter adapter);

    /**
     * Display information about a device
     */
    void InspectDevice(wgpu::Device device);

    /**
     * Helper function to get texture format
     */
    wgpu::TextureFormat GetTextureFormat(wgpu::Surface surface, wgpu::Adapter adapter);

    inline wgpu::StringView GenerateString(const char* str)
    {
        return {str, strlen(str)};
    }

}  // namespace WebGPUUtils
