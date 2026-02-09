#pragma once

#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>

/**
 * Get a WGPUSurface from a SDL3 window.
 */
wgpu::Surface SDL_GetWGPUSurface(wgpu::Instance instance, SDL_Window* window);
