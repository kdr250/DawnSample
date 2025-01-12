#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

#include "Application.h"

int main(int argc, char* argv[])
{
    Application app;

    if (!app.Initialize())
    {
        return EXIT_FAILURE;
    }

#ifdef __EMSCRIPTEN__
    auto callback = [](void* arg)
    {
        Application* pApp = reinterpret_cast<Application*>(arg);
        pApp->MainLoop();
    };
    emscripten_set_main_loop_arg(callback, &app, 60, true);
#else
    while (app.IsRunning())
    {
        app.MainLoop();
    }
    app.Terminate();
#endif

    return EXIT_SUCCESS;
}
