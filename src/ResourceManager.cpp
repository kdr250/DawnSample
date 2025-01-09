#include "ResourceManager.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

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
            for (int i = 0; i < dimensions + 3; ++i)  // Get x, y, r, g, b
            {
                iss >> value;
                pointData.emplace_back(value);
            }
        }
        else if (currentSection == Section::Indices)
        {
            std::istringstream iss(line);
            for (int i = 0; i < 3; ++i)  // Get corners #0 #1 and #2
            {
                iss >> index;
                indexData.emplace_back(index);
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
    std::string error;

    bool result =
        tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &error, path.string().c_str());

    if (!warn.empty())
    {
        std::cout << warn << std::endl;
    }

    if (!error.empty())
    {
        std::cerr << error << std::endl;
    }

    if (!result)
    {
        return false;
    }

    // Filling in vertexData:
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

            vertexData[offset + i].color = {
                attrib.colors[3 * index.vertex_index + 0],
                attrib.colors[3 * index.vertex_index + 1],
                attrib.colors[3 * index.vertex_index + 2],
            };

            vertexData[offset + i].uv = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1 - attrib.texcoords[2 * index.texcoord_index + 1],
            };
        }
    }

    return true;
}

static uint32_t bit_width(uint32_t m)
{
    if (m == 0)
        return 0;
    else
    {
        uint32_t w = 0;
        while (m >>= 1)
            ++w;
        return w;
    }
}

WGPUTexture ResourceManager::LoadTexture(const std::filesystem::path& path,
                                         WGPUDevice device,
                                         WGPUTextureView* pTextureView)
{
    int width, height, channels;
    unsigned char* pixelData = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixelData == nullptr)
    {
        return nullptr;
    }

    WGPUTextureDescriptor textureDesc;
    textureDesc.nextInChain = nullptr;
    textureDesc.label       = WebGPUUtils::GenerateString("Texture");
    textureDesc.dimension   = WGPUTextureDimension_2D;
    textureDesc.format      = WGPUTextureFormat_RGBA8Unorm;
    textureDesc.size        = {(unsigned int)width, (unsigned int)height, 1};
    textureDesc.mipLevelCount =
        bit_width(std::max(textureDesc.size.width, textureDesc.size.height));
    textureDesc.sampleCount     = 1;
    textureDesc.usage           = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    textureDesc.viewFormatCount = 0;
    textureDesc.viewFormats     = nullptr;
    WGPUTexture texture         = wgpuDeviceCreateTexture(device, &textureDesc);

    // Upload data to the GPU texture
    WriteMipMaps(device, texture, textureDesc.size, textureDesc.mipLevelCount, pixelData);

    stbi_image_free(pixelData);

    if (pTextureView)
    {
        WGPUTextureViewDescriptor textureViewDesc;
        textureViewDesc.nextInChain     = nullptr;
        textureViewDesc.label           = WebGPUUtils::GenerateString("Texture View");
        textureViewDesc.aspect          = WGPUTextureAspect_All;
        textureViewDesc.baseArrayLayer  = 0;
        textureViewDesc.arrayLayerCount = 1;
        textureViewDesc.baseMipLevel    = 0;
        textureViewDesc.mipLevelCount   = textureDesc.mipLevelCount;
        textureViewDesc.dimension       = WGPUTextureViewDimension_2D;
        textureViewDesc.format          = textureDesc.format;
#ifndef __EMSCRIPTEN__
        textureViewDesc.usage = WGPUTextureUsage_None;
#endif
        *pTextureView = wgpuTextureCreateView(texture, &textureViewDesc);
    }

    return texture;
}

void ResourceManager::WriteMipMaps(WGPUDevice device,
                                   WGPUTexture texture,
                                   WGPUExtent3D textureSize,
                                   uint32_t mipLevelCount,
                                   const unsigned char* pixelData)
{
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    WGPUImageCopyTexture destination;
    destination.texture = texture;
    destination.origin  = {0, 0, 0};
    destination.aspect  = WGPUTextureAspect_All;

    WGPUTextureDataLayout source;
    source.offset = 0;

    WGPUExtent3D mipLevelSize = textureSize;
    std::vector<unsigned char> previousLevelPixels;
    WGPUExtent3D previousMipLevelSize;
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
        wgpuQueueWriteTexture(queue,
                              &destination,
                              pixels.data(),
                              pixels.size(),
                              &source,
                              &mipLevelSize);

        previousLevelPixels  = std::move(pixels);
        previousMipLevelSize = mipLevelSize;
        mipLevelSize.width /= 2;
        mipLevelSize.height /= 2;
    }

    wgpuQueueRelease(queue);
}

WGPUShaderModule ResourceManager::LoadShaderModule(const std::filesystem::path& path,
                                                   WGPUDevice device)
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

    WGPUShaderModuleWGSLDescriptor shaderCodeDesc {};
    shaderCodeDesc.chain.next = nullptr;
#ifdef __EMSCRIPTEN__
    shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
#else
    shaderCodeDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
#endif
    shaderCodeDesc.code = WebGPUUtils::GenerateString(shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc {};
    shaderDesc.nextInChain = &shaderCodeDesc.chain;

    return wgpuDeviceCreateShaderModule(device, &shaderDesc);
}
