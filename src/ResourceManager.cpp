#include "ResourceManager.h"

#include <fstream>
#include <sstream>
#include <string>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

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

            vertexData[offset + i].color = {
                attrib.colors[3 * index.vertex_index + 0],
                attrib.colors[3 * index.vertex_index + 1],
                attrib.colors[3 * index.vertex_index + 2],
            };
        }
    }

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
