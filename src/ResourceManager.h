#pragma once

#include <webgpu/webgpu_cpp.h>
#include <filesystem>
#include <vector>

class ResourceManager
{
public:
    static bool LoadGeometry(const std::filesystem::path& path,
                             std::vector<float>& pointData,
                             std::vector<uint16_t>& indexData);

    static wgpu::ShaderModule LoadShaderModule(const std::filesystem::path& path,
                                               wgpu::Device device);
};
