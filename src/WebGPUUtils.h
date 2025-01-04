#pragma once

#include <webgpu/webgpu.h>

namespace WebGPUUtils
{
    /**
     * Utility function to get a WebGPU adapter
     */
    WGPUAdapter RequestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const* options);

    /**
     * An example of how we can inspect the capabilities of the hardware through
     * the adapter object.
     */
    void InspectAdapter(WGPUAdapter adapter);

}  // namespace WebGPUUtils
