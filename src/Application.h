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

    void UpdateViewMatrix();

    // Mouse
    void OnMouseMove();
    void OnMouseButton(SDL_Event& event);
    void OnScroll(SDL_Event& event);

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

    struct CameraState
    {
        // angles.x is the rotation of the camera around the global vertical axis, affected by mouse.x
        // angles.y is the rotation of the camera around its local horizontal axis, affected by mouse.y
        glm::vec2 angles = {0.8f, 0.5f};
        // zoom is the position of the camera along its local forward axis, affected by the scroll wheel
        float zoom = -1.2f;
    };

    struct DragState
    {
        // Whether a drag action is ongoing (i.e., we are between mouse press and mouse release)
        bool active = false;
        // The position of the mouse at the beginning of the drag action
        glm::vec2 startMouse;
        // The camera state at the beginning of the drag action
        CameraState startCameraState;

        // Constant settings
        float sensitivity       = 0.01f;
        float scrollSensitivity = 0.1f;

        // Inertia
        glm::vec2 velocity = {0.0, 0.0};
        glm::vec2 previousDelta;
        float intertia = 0.9f;
    };

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

    CameraState cameraState;
    DragState dragState;

    bool isRunning = true;
};
