#pragma once

#include <webgpu/webgpu_cpp.h>
#include <filesystem>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
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

    static wgpu::Texture LoadTexture(const std::filesystem::path& path,
                                     wgpu::Device device,
                                     wgpu::TextureView* pTextureView = nullptr);

private:
    static void PopulateTextureFrameAttributes(std::vector<VertexAttributes>& vertexData);

    /**
	 * Compute the TBN local to a triangle face from its corners and return it as
	 * a matrix whose columns are the T, B and N vectors
	 */
    static glm::mat3x3 ComputeTBN(const VertexAttributes corners[3], const glm::vec3& expectedN);

    static void WriteMipMaps(wgpu::Device device,
                             wgpu::Texture texture,
                             wgpu::Extent3D textureSize,
                             uint32_t mipLevelCount,
                             const unsigned char* pixelData);
};
