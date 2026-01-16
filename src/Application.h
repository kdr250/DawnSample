#pragma once

#include <SDL2/SDL.h>
#include <webgpu/webgpu_cpp.h>
#include <array>
#include <cassert>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/polar_coordinates.hpp>

struct VertexAttributes
{
    glm::vec3 position;
    // Texture mapping attributes represent the local frame in which
    // normals sampled from the normal map are expressed.
    glm::vec3 tangent;    // T = local X axis
    glm::vec3 bitangent;  // B = local Y axis
    glm::vec3 normal;     // N = local Z axis

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

    bool InitializeBindGroupLayout();

    bool InitializePipeline();

    bool InitializeTexture();

    bool InitializeGeometry();

    bool InitializeUniforms();

    bool InitializeLightingUniforms();

    bool InitializeBindGroups();

    bool InitializeGUI();

    wgpu::TextureView GetNextSurfaceTextureView();

    void SetDefaultLimits(wgpu::Limits& limits) const;

    wgpu::Limits GetRequiredLimits(wgpu::Adapter adapter) const;

    void SetDefaultBindGroupLayout(wgpu::BindGroupLayoutEntry& bindingLayout);

    void SetDefaultStencilFaceState(wgpu::StencilFaceState& stencilFaceState);
    void SetDefaultDepthStencilState(wgpu::DepthStencilState& depthStencilstate);

    // Camera
    void UpdateViewMatrix();
    void OnMouseMove();
    void OnMouseButton(SDL_Event& event);
    void OnScroll(SDL_Event& event);

    // Lighting
    void UpdateLightingUniforms();

    // GUI
    void TerminateGUI();
    void UpdateGUI(wgpu::RenderPassEncoder renderPass);

    struct MyUniforms
    {
        glm::mat4x4 projectionMatrix;
        glm::mat4x4 viewMatrix;
        glm::mat4x4 modelMatrix;
        glm::vec4 color;
        glm::vec3 cameraWorldPosition;
        float time;
    };

    static_assert(sizeof(MyUniforms) % 16 == 0);

    struct LightingUniforms
    {
        std::array<glm::vec4, 2> directions;
        std::array<glm::vec4, 2> colors;
        float hardness = 32.0f;
        float kd       = 1.0f;
        float ks       = 0.5f;
        float _pad[1];
    };

    static_assert(sizeof(LightingUniforms) % 16 == 0);

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

    SDL_Window* window                     = nullptr;
    wgpu::Device device                    = nullptr;
    wgpu::Queue queue                      = nullptr;
    wgpu::Surface surface                  = nullptr;
    wgpu::RenderPipeline pipeline          = nullptr;
    wgpu::TextureFormat surfaceFormat      = wgpu::TextureFormat::Undefined;
    wgpu::Buffer pointBuffer               = nullptr;
    uint32_t indexCount                    = 0;
    wgpu::Buffer uniformBuffer             = nullptr;
    wgpu::Buffer lightingUniformBuffer     = nullptr;
    wgpu::PipelineLayout layout            = nullptr;
    wgpu::BindGroupLayout bindGroupLayout  = nullptr;
    wgpu::BindGroup bindGroup              = nullptr;
    wgpu::TextureFormat depthTextureFormat = wgpu::TextureFormat::Depth24Plus;
    wgpu::Texture depthTexture             = nullptr;
    wgpu::TextureView depthTextureView     = nullptr;
    wgpu::Texture baseColorTexture         = nullptr;
    wgpu::TextureView baseColorTextureView = nullptr;
    wgpu::Texture normalTexture            = nullptr;
    wgpu::TextureView normalTextureView    = nullptr;
    wgpu::Sampler sampler                  = nullptr;

    MyUniforms uniforms;
    LightingUniforms lightingUniforms;
    bool lightingUniformsChanged = true;

    CameraState cameraState;
    DragState dragState;

    bool isRunning = true;

    Uint64 tickCount = 0;
    float deltaTime  = 0.0f;
};
