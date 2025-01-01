#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>

#include "sdl2webgpu.h"

int main(int argc, char* argv[])
{
    // Init WebGPU
    WGPUInstanceDescriptor desc {};
    desc.nextInChain      = NULL;
    WGPUInstance instance = wgpuCreateInstance(&desc);
    if (instance == nullptr)
    {
        SDL_Log("Instance creation failed!");
        return EXIT_FAILURE;
    }

    // Init SDL
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Dawn Sample",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          1024,
                                          768,
                                          0);

    // Create WebGPU surface
    WGPUSurface surface = SDL_GetWGPUSurface(instance, window);

    // Main loop
    bool isRunning = true;
    while (isRunning)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    isRunning = false;
                    break;

                default:
                    break;
            }
        }

        const Uint8* state = SDL_GetKeyboardState(NULL);
        if (state[SDL_SCANCODE_ESCAPE])
        {
            isRunning = false;
        }
    }

    // Terminate SDL
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
