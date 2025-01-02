#include <SDL2/SDL.h>
#include <webgpu/webgpu.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "sdl2webgpu.h"

SDL_Window* window;
bool isRunning = true;

void Quit()
{
    // Terminate SDL
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void MainLoop()
{
    if (!isRunning)
    {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop(); /* this should "kill" the app. */
        Quit();
        return;
#endif
    }

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

int main(int argc, char* argv[])
{
    // Init WebGPU
    WGPUInstance instance = wgpuCreateInstance(nullptr);
    if (instance == nullptr)
    {
        SDL_Log("Instance creation failed!");
        return EXIT_FAILURE;
    }

    // Init SDL
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Dawn Sample",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              1024,
                              768,
                              0);

    // Create WebGPU surface
    WGPUSurface surface = SDL_GetWGPUSurface(instance, window);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(MainLoop, 0, 1);
#else
    while (isRunning)
    {
        MainLoop();
    }
    Quit();
#endif

    return EXIT_SUCCESS;
}
