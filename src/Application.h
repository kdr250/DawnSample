#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu_cpp.h>
#include <array>
#include <cassert>

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

    void InitializeBuffers();

    void InitializeBindGroups();

    wgpu::TextureView GetNextSurfaceTextureView();

    void SetDefaultLimits(wgpu::Limits& limits) const;
    wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;

    void SetDefaultBindGroupLayout(wgpu::BindGroupLayoutEntry& bindingLayout);

    void SetDefaultStencilFaceState(wgpu::StencilFaceState& stencilFaceState);
    void SetDefaultDepthStencilState(wgpu::DepthStencilState& depthStencilstate);

    struct MyUniforms
    {
        std::array<float, 4> color;
        float time;
        float _padding[3];
    };

    static_assert(sizeof(MyUniforms) % 16 == 0);

    SDL_Window* window;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    wgpu::RenderPipeline pipeline;
    wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
    wgpu::Buffer pointBuffer;
    wgpu::Buffer indexBuffer;
    uint32_t indexCount;
    wgpu::Buffer uniformBuffer;
    wgpu::PipelineLayout layout;
    wgpu::BindGroupLayout bindGroupLayout;
    wgpu::BindGroup bindGroup;
    wgpu::Texture depthTexture;
    wgpu::TextureView depthTextureView;

    bool isRunning = true;

    Uint64 tickCount = 0;
    float deltaTime  = 0.0f;
};
