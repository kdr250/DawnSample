#include "ResourceManager.h"

#include <fstream>
#include <sstream>
#include <string>

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
