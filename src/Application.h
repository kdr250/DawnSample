#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu_cpp.h>
#include <array>
#include <cassert>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>

struct VertexAttributes
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
};

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
    bool InitializeWindowAndDevice();

    bool InitializeDepthBuffer();

    bool InitializePipeline();

    bool InitializeTexture();

    bool InitializeGeometry();

    bool InitializeUniforms();

    bool InitializeBindGroups();

    wgpu::TextureView GetNextSurfaceTextureView();

    void SetDefaultLimits(wgpu::Limits& limits) const;
    wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;

    void SetDefaultBindGroupLayout(wgpu::BindGroupLayoutEntry& bindingLayout);

    void SetDefaultStencilFaceState(wgpu::StencilFaceState& stencilFaceState);
    void SetDefaultDepthStencilState(wgpu::DepthStencilState& depthStencilstate);

    struct MyUniforms
    {
        glm::mat4x4 projectionMatrix;
        glm::mat4x4 viewMatrix;
        glm::mat4x4 modelMatrix;
        glm::vec4 color;
        float time;
        float _padding[3];
    };

    static_assert(sizeof(MyUniforms) % 16 == 0);

    SDL_Window* window                     = nullptr;
    wgpu::Device device                    = nullptr;
    wgpu::Queue queue                      = nullptr;
    wgpu::Surface surface                  = nullptr;
    wgpu::RenderPipeline pipeline          = nullptr;
    wgpu::TextureFormat surfaceFormat      = wgpu::TextureFormat::Undefined;
    wgpu::Buffer pointBuffer               = nullptr;
    uint32_t indexCount                    = 0;
    wgpu::Buffer uniformBuffer             = nullptr;
    wgpu::PipelineLayout layout            = nullptr;
    wgpu::BindGroupLayout bindGroupLayout  = nullptr;
    wgpu::BindGroup bindGroup              = nullptr;
    wgpu::TextureFormat depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
    wgpu::Texture depthTexture             = nullptr;
    wgpu::TextureView depthTextureView     = nullptr;
    wgpu::Texture texture                  = nullptr;
    wgpu::TextureView textureView          = nullptr;
    wgpu::Sampler sampler                  = nullptr;

    MyUniforms uniforms;

    bool isRunning = true;

    Uint64 tickCount = 0;
    float deltaTime  = 0.0f;
};
