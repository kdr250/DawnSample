#include "ResourceManager.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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
        }
    }

    return true;
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
