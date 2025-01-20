#pragma once

#include <webgpu/webgpu_cpp.h>
#include <filesystem>
#include <vector>

struct VertexAttributes;

class ResourceManager
{
public:
    static bool LoadGeometry(const std::filesystem::path& path,
                             std::vector<float>& pointData,
                             std::vector<uint16_t>& indexData,
                             int dimensions);

    static bool LoadGeometryFromObj(const std::filesystem::path& path,
                                    std::vector<VertexAttributes>& vertexData);

    static wgpu::ShaderModule LoadShaderModule(const std::filesystem::path& path,
                                               wgpu::Device device);
};
