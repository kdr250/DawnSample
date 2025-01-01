#ifndef _sdl2_webgpu_h_
#define _sdl2_webgpu_h_

#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * Get a WGPUSurface from a SDL2 window.
 */
    WGPUSurface SDL_GetWGPUSurface(WGPUInstance instance, SDL_Window* window);

#ifdef __cplusplus
}
#endif

#endif  // _sdl2_webgpu_h_
