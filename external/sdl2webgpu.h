#ifndef _sdl2_webgpu_h_
#define _sdl2_webgpu_h_

#include <SDL2/SDL.h>
#include <webgpu/webgpu_cpp.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * Get a WGPUSurface from a SDL2 window.
 */
    wgpu::Surface SDL_GetWGPUSurface(wgpu::Instance instance, SDL_Window* window);

#ifdef __cplusplus
}
#endif

#endif  // _sdl2_webgpu_h_
