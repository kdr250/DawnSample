#include "ResourceManager.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <sstream>
#include <string>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Application.h"
#include "WebGPUUtils.h"

bool ResourceManager::LoadGeometry(const std::filesystem::path& path,
                                   std::vector<float>& pointData,
                                   std::vector<uint16_t>& indexData,
                                   int dimensions)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    pointData.clear();
    indexData.clear();

    enum class Section
    {
        None,
        Points,
        Indices,
    };
    Section currentSection = Section::None;

    float value;
    uint16_t index;
    std::string line;
    while (!file.eof())
    {
        getline(file, line);

        // overcome the `CRLF` problem
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line == "[points]")
        {
            currentSection = Section::Points;
        }
        else if (line == "[indices]")
        {
            currentSection = Section::Indices;
        }
        else if (line[0] == '#' || line.empty())
        {
            // Do nothing, this is a comment
        }
        else if (currentSection == Section::Points)
        {
            std::istringstream iss(line);
            // Get x, y, z, r, g, b
            for (int i = 0; i < dimensions + 3; ++i)
            {
                iss >> value;
                pointData.push_back(value);
            }
        }
        else if (currentSection == Section::Indices)
        {
            std::istringstream iss(line);
            // Get corners #0 #1 and #2
            for (int i = 0; i < 3; ++i)
            {
                iss >> index;
                indexData.push_back(index);
            }
        }
    }
    return true;
}

bool ResourceManager::LoadGeometryFromObj(const std::filesystem::path& path,
                                          std::vector<VertexAttributes>& vertexData)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    bool result =
        tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str());

    if (!warn.empty())
    {
        SDL_Log("%s", warn.c_str());
    }
    if (!err.empty())
    {
        SDL_Log("%s", err.c_str());
    }

    if (!result)
    {
        return false;
    }

    // Filling in vertexData
    vertexData.clear();
    for (const auto& shape : shapes)
    {
        size_t offset = vertexData.size();
        vertexData.resize(offset + shape.mesh.indices.size());

        for (size_t i = 0; i < shape.mesh.indices.size(); ++i)
        {
            const tinyobj::index_t& index = shape.mesh.indices[i];

            vertexData[offset + i].position = {
                attrib.vertices[3 * index.vertex_index + 0],
                -attrib.vertices[3 * index.vertex_index + 2],  // Add a minus to avoid mirroring
                attrib.vertices[3 * index.vertex_index + 1],
            };

            vertexData[offset + i].normal = {
                attrib.normals[3 * index.normal_index + 0],
                -attrib.normals[3 * index.normal_index + 2],
                attrib.normals[3 * index.normal_index + 1],
            };

            vertexData[offset + i].uv = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1 - attrib.texcoords[2 * index.texcoord_index + 1],
            };

            vertexData[offset + i].color = {
                attrib.colors[3 * index.vertex_index + 0],
                attrib.colors[3 * index.vertex_index + 1],
                attrib.colors[3 * index.vertex_index + 2],
            };
        }
    }

    PopulateTextureFrameAttributes(vertexData);
    return true;
}

wgpu::ShaderModule ResourceManager::LoadShaderModule(const std::filesystem::path& path,
                                                     wgpu::Device device)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return nullptr;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    std::string shaderSource(size, ' ');
    file.seekg(0);
    file.read(shaderSource.data(), size);

    wgpu::ShaderModuleWGSLDescriptor shaderCodeDesc {};
    shaderCodeDesc.nextInChain = nullptr;
#ifdef __EMSCRIPTEN__
    shaderCodeDesc.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
#else
    shaderCodeDesc.sType = wgpu::SType::ShaderSourceWGSL;
#endif
    shaderCodeDesc.code = WebGPUUtils::GenerateString(shaderSource.c_str());

    wgpu::ShaderModuleDescriptor shaderDesc {};
    shaderDesc.nextInChain = &shaderCodeDesc;

    wgpu::ShaderModule shaderMdoule = device.CreateShaderModule(&shaderDesc);

    return shaderMdoule;
}

wgpu::Texture ResourceManager::LoadTexture(const std::filesystem::path& path,
                                           wgpu::Device device,
                                           wgpu::TextureView* pTextureView)
{
    int width, height, channels;
    unsigned char* pixelData =
        stbi_load(path.string().c_str(), &width, &height, &channels, 4 /* force 4 channels */);

    if (pixelData == nullptr)
    {
        return nullptr;
    }

    wgpu::TextureDescriptor textureDesc;
    textureDesc.nextInChain = nullptr;
    textureDesc.label       = WebGPUUtils::GenerateString("Texture");
    textureDesc.dimension   = wgpu::TextureDimension::e2D;
    textureDesc.format      = wgpu::TextureFormat::RGBA8Unorm;
    textureDesc.size        = {(unsigned int)width, (unsigned int)height, 1};
    textureDesc.mipLevelCount =
        std::bit_width(std::max(textureDesc.size.width, textureDesc.size.height));
    textureDesc.sampleCount     = 1;
    textureDesc.usage           = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    textureDesc.viewFormatCount = 0;
    textureDesc.viewFormats     = nullptr;
    wgpu::Texture texture       = device.CreateTexture(&textureDesc);

    // Upload data to the GPU texture (to be implemented!)
    WriteMipMaps(device, texture, textureDesc.size, textureDesc.mipLevelCount, pixelData);

    stbi_image_free(pixelData);

    if (pTextureView)
    {
        wgpu::TextureViewDescriptor textureViewDesc;
        textureViewDesc.aspect          = wgpu::TextureAspect::All;
        textureViewDesc.baseArrayLayer  = 0;
        textureViewDesc.arrayLayerCount = 1;
        textureViewDesc.baseMipLevel    = 0;
        textureViewDesc.mipLevelCount   = textureDesc.mipLevelCount;
        textureViewDesc.dimension       = wgpu::TextureViewDimension::e2D;
        textureViewDesc.format          = textureDesc.format;
#ifndef __EMSCRIPTEN__
        textureViewDesc.usage = wgpu::TextureUsage::None;
#endif
        *pTextureView = texture.CreateView(&textureViewDesc);
    }

    return texture;
}

void ResourceManager::PopulateTextureFrameAttributes(std::vector<VertexAttributes>& vertexData)
{
    size_t triangleCount = vertexData.size() / 3;
    // We compute the local texture frame per triangle
    for (int t = 0; t < triangleCount; ++t)
    {
        VertexAttributes* v = &vertexData[3 * t];

        // We assign these to the 3 corners of the triangle
        for (int k = 0; k < 3; ++k)
        {
            glm::mat3x3 TBN = ComputeTBN(v, v[k].normal);
            v[k].tangent    = TBN[0];
            v[k].bitangent  = TBN[1];
        }
    }
}

glm::mat3x3 ResourceManager::ComputeTBN(const VertexAttributes corners[3],
                                        const glm::vec3& expectedN)
{
    // What we call e in the figure
    glm::vec3 ePos1 = corners[1].position - corners[0].position;
    glm::vec3 ePos2 = corners[2].position - corners[0].position;

    // What we call \bar e in the figure
    glm::vec2 eUV1 = corners[1].uv - corners[0].uv;
    glm::vec2 eUV2 = corners[2].uv - corners[0].uv;

    glm::vec3 T = normalize(ePos1 * eUV2.y - ePos2 * eUV1.y);
    glm::vec3 B = normalize(ePos2 * eUV1.x - ePos1 * eUV2.x);
    glm::vec3 N = glm::cross(T, B);

    // Fix overall orientation
    if (glm::dot(N, expectedN) < 0.0)
    {
        T = -T;
        B = -B;
        N = -N;
    }

    // Ortho-normalize the (T, B, expectedN) frame
    // a. "Remove" the part of T that is along expected N
    N = expectedN;
    T = glm::normalize(T - glm::dot(T, N) * N);
    // b. Recompute B from N and T
    B = glm::cross(N, T);

    return glm::mat3x3(T, B, N);
}

void ResourceManager::WriteMipMaps(wgpu::Device device,
                                   wgpu::Texture texture,
                                   wgpu::Extent3D textureSize,
                                   uint32_t mipLevelCount,
                                   const unsigned char* pixelData)
{
    wgpu::Queue queue = device.GetQueue();

    // Arguments telling which part of the texture we upload to
    wgpu::ImageCopyTexture destination;
    destination.texture = texture;
    destination.origin  = {0, 0, 0};
    destination.aspect  = wgpu::TextureAspect::All;

    // Arguments telling how the C++ side pixel memory is laid out
    wgpu::TextureDataLayout source;
    source.offset = 0;

    // Create image data
    wgpu::Extent3D mipLevelSize = textureSize;
    std::vector<unsigned char> previousLevelPixels;
    wgpu::Extent3D previousMipLevelSize;
    for (uint32_t level = 0; level < mipLevelCount; ++level)
    {
        // Pixel data for the current level
        std::vector<unsigned char> pixels(4 * mipLevelSize.width * mipLevelSize.height);
        if (level == 0)
        {
            // We cannot really avoid this copy since we need this
            // in previousLevelPixels at the next iteration
            memcpy(pixels.data(), pixelData, pixels.size());
        }
        else
        {
            // Create mip level data
            for (uint32_t i = 0; i < mipLevelSize.width; ++i)
            {
                for (uint32_t j = 0; j < mipLevelSize.height; ++j)
                {
                    unsigned char* p = &pixels[4 * (j * mipLevelSize.width + i)];
                    // Get the corresponding 4 pixels from the previous level
                    unsigned char* p00 =
                        &previousLevelPixels[4
                                             * ((2 * j + 0) * previousMipLevelSize.width
                                                + (2 * i + 0))];
                    unsigned char* p01 =
                        &previousLevelPixels[4
                                             * ((2 * j + 0) * previousMipLevelSize.width
                                                + (2 * i + 1))];
                    unsigned char* p10 =
                        &previousLevelPixels[4
                                             * ((2 * j + 1) * previousMipLevelSize.width
                                                + (2 * i + 0))];
                    unsigned char* p11 =
                        &previousLevelPixels[4
                                             * ((2 * j + 1) * previousMipLevelSize.width
                                                + (2 * i + 1))];
                    // Average
                    p[0] = (p00[0] + p01[0] + p10[0] + p11[0]) / 4;
                    p[1] = (p00[1] + p01[1] + p10[1] + p11[1]) / 4;
                    p[2] = (p00[2] + p01[2] + p10[2] + p11[2]) / 4;
                    p[3] = (p00[3] + p01[3] + p10[3] + p11[3]) / 4;
                }
            }
        }

        // Upload data to the GPU texture
        destination.mipLevel = level;
        source.bytesPerRow   = 4 * mipLevelSize.width;
        source.rowsPerImage  = mipLevelSize.height;
        queue.WriteTexture(&destination, pixels.data(), pixels.size(), &source, &mipLevelSize);

        previousLevelPixels  = std::move(pixels);
        previousMipLevelSize = mipLevelSize;
        mipLevelSize.width /= 2;
        mipLevelSize.height /= 2;
    }
}
