#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>

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
    void InitializePipeline();

    WGPUTextureView GetNextSurfaceTextureView();

    SDL_Window* window;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSurface surface;
    WGPURenderPipeline pipeline;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    bool isRunning                  = true;
};
