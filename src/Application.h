#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu_cpp.h>

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
    void SetDefaultLimits(wgpu::Limits& limits) const;
    wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;

    void InitializePipeline();

    void InitializeBuffers();

    wgpu::TextureView GetNextSurfaceTextureView();

    SDL_Window* window;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    wgpu::RenderPipeline pipeline;
    wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
    wgpu::Buffer pointBuffer;
    wgpu::Buffer indexBuffer;
    uint32_t indexCount;

    bool isRunning = true;

    Uint64 tickCount = 0;
    float deltaTime  = 0.0f;
};
