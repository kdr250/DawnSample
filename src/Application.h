#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>
#include <array>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>

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
        // We add transform matrices
        glm::mat4x4 projectionMatrix;
        glm::mat4x4 viewMatrix;
        glm::mat4x4 modelMatrix;
        std::array<float, 4> color;
        float time;
        float _pad[3];
    };

    struct VertexAttributes
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 color;
    };

    static_assert(sizeof(MyUniforms) % 16 == 0);  // Have the compiler check byte alignment

    void SetDefaultLimits(WGPULimits& limits) const;
    WGPURequiredLimits GetRequiredLimits(WGPUAdapter adapter) const;

    void SetDefaultBindGroupLayout(WGPUBindGroupLayoutEntry& bindingLayout);

    void SetDefaultStencilFaceState(WGPUStencilFaceState& stencilFaceState);
    void SetDefaultDepthStencilState(WGPUDepthStencilState& depthStencilState);

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
    WGPUTexture depthTexture;
    WGPUTextureView depthTextureView;

    MyUniforms uniforms;

    bool isRunning = true;
};
