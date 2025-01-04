#pragma once

#include <webgpu/webgpu.h>

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

}  // namespace WebGPUUtils
