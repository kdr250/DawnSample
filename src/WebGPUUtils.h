#pragma once

#include <webgpu/webgpu.h>
#include <string>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

namespace WebGPUUtils
{
    /**
     * Utility function to get a WebGPU adapter
     */
    WGPUAdapter RequestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const* options);

    /**
     * Utility function to get a WebGPU device
     */
    WGPUDevice RequestDeviceSync(WGPUAdapter adapter, WGPUDeviceDescriptor const* descripter);

    /**
     * An example of how we can inspect the capabilities of the hardware through
     * the adapter object.
     */
    void InspectAdapter(WGPUAdapter adapter);

    /**
     * Display information about a device
     */
    void InspectDevice(WGPUDevice device);

    /**
     * Helper function to get texture format
     */
    WGPUTextureFormat GetTextureFormat(WGPUSurface surface, WGPUAdapter adapter);

    /**
     * Helper function for const char* and WGPUStringView
     */
#ifdef __EMSCRIPTEN__
    inline const char* GenerateString(const char* str)
    {
        return str;
    }
#else
    inline WGPUStringView GenerateString(const char* str)
    {
        return {str, strlen(str)};
    }
#endif

}  // namespace WebGPUUtils
