#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>
#include <array>

class Application
{
public:
    // Initialize everything and return true if it went all right
    bool Initialize();

    // Uninitialize everything that was initialized
    void Terminate();

    // Draw a frame and handle events
    void MainLoop();

    // Return true as long as the main loop should keep on running
    bool IsRunning();

private:
    struct MyUniforms
    {
        std::array<float, 4> color;
        float time;
        float _padding[3];
    };

    static_assert(sizeof(MyUniforms) % 16 == 0);  // Have the compiler check byte alignment

    void SetDefaultLimits(WGPULimits& limits) const;
    WGPURequiredLimits GetRequiredLimits(WGPUAdapter adapter) const;

    void SetDefaultBindGroupLayout(WGPUBindGroupLayoutEntry& bindingLayout);

    void InitializePipeline();

    void InitializeBuffers();

    void InitializeBindGroups();

    WGPUTextureView GetNextSurfaceTextureView();

    SDL_Window* window;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSurface surface;
    WGPURenderPipeline pipeline;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    WGPUBuffer pointBuffer;
    WGPUBuffer indexBuffer;
    uint32_t indexCount;
    WGPUBuffer uniformBuffer;
    WGPUBindGroupLayout bindGroupLayout;
    WGPUBindGroup bindGroup;
    WGPUPipelineLayout layout;

    bool isRunning = true;
};
