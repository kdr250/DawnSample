#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>
#include <array>

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
    void TerminateWindowAndDevice();

    bool InitializeDepthBuffer();
    void TerminateDepthBuffer();

    bool InitializePipeline();
    void TerminatePipeline();

    bool InitializeTexture();
    void TerminateTexture();

    bool InitializeGeometry();
    void TerminateGeometry();

    bool InitializeUniforms();
    void TerminateUniforms();

    bool InitializeBindGroups();
    void TerminateBindGroups();

    void SetDefaultLimits(WGPULimits& limits) const;
    WGPURequiredLimits GetRequiredLimits(WGPUAdapter adapter) const;

    void SetDefaultBindGroupLayout(WGPUBindGroupLayoutEntry& bindingLayout);

    void SetDefaultStencilFaceState(WGPUStencilFaceState& stencilFaceState);
    void SetDefaultDepthStencilState(WGPUDepthStencilState& depthStencilState);

    WGPUTextureView GetNextSurfaceTextureView();

    struct MyUniforms
    {
        // We add transform matrices
        glm::mat4x4 projectionMatrix;
        glm::mat4x4 viewMatrix;
        glm::mat4x4 modelMatrix;
        std::array<float, 4> color;
        float time;
        float _pad[3];
    };

    static_assert(sizeof(MyUniforms) % 16 == 0);  // Have the compiler check byte alignment

    SDL_Window* window                   = nullptr;
    WGPUDevice device                    = nullptr;
    WGPUQueue queue                      = nullptr;
    WGPUSurface surface                  = nullptr;
    WGPURenderPipeline pipeline          = nullptr;
    WGPUTextureFormat surfaceFormat      = WGPUTextureFormat_Undefined;
    WGPUBuffer vertexBuffer              = nullptr;
    uint32_t indexCount                  = 0;
    WGPUBuffer uniformBuffer             = nullptr;
    WGPUBindGroupLayout bindGroupLayout  = nullptr;
    WGPUBindGroup bindGroup              = nullptr;
    WGPUPipelineLayout layout            = nullptr;
    WGPUTexture depthTexture             = nullptr;
    WGPUTextureView depthTextureView     = nullptr;
    WGPUTextureFormat depthTextureFormat = WGPUTextureFormat_Depth24Plus;
    WGPUTexture texture                  = nullptr;
    WGPUTextureView textureView          = nullptr;
    WGPUSampler sampler                  = nullptr;

    MyUniforms uniforms;

    bool isRunning = true;
};
