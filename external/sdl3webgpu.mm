#include "sdl3webgpu.h"

#include <webgpu/webgpu_cpp.h>

#ifdef SDL_PLATFORM_MACOS
    #include <Cocoa/Cocoa.h>
    #include <Foundation/Foundation.h>
    #include <QuartzCore/CAMetalLayer.h>
#endif
#ifdef SDL_PLATFORM_IOS
    #include <Foundation/Foundation.h>
    #include <Metal/Metal.h>
    #include <QuartzCore/CAMetalLayer.h>
    #include <UIKit/UIKit.h>
#endif
#ifdef SDL_PLATFORM_WIN32
    #include <windows.h>
#endif

#include <SDL3/SDL.h>

wgpu::Surface SDL_GetWGPUSurface(wgpu::Instance instance, SDL_Window* window)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(window);

#ifdef SDL_PLATFORM_MACOS
    {
        id metal_layer      = NULL;
        NSWindow *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
        if (!ns_window) return nullptr;
        [ns_window.contentView setWantsLayer:YES];
        metal_layer = [CAMetalLayer layer];
        [ns_window.contentView setLayer:metal_layer];

        wgpu::SurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.nextInChain = nullptr;
        fromMetalLayer.sType = wgpu::SType::SurfaceSourceMetalLayer;
        fromMetalLayer.layer      = metal_layer;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer;
        surfaceDescriptor.label = { NULL, WGPU_STRLEN };

        return instance.CreateSurface(&surfaceDescriptor);
    }
#endif
#ifdef SDL_PLATFORM_IOS
    {
        UIWindow *ui_window = (__bridge UIWindow *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
        if (!uiwindow) return nullptr;

        UIView* ui_view           = ui_window.rootViewController.view;
        CAMetalLayer* metal_layer = [CAMetalLayer new];
        metal_layer.opaque        = true;
        metal_layer.frame         = ui_view.frame;
        metal_layer.drawableSize  = ui_view.frame.size;

        [ui_view.layer addSublayer:metal_layer];

        wgpu::SurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.nextInChain  = nullptr;
        fromMetalLayer.sType = wgpu::SType::SurfaceSourceMetalLayer;
        fromMetalLayer.layer       = metal_layer;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer;
        surfaceDescriptor.label = { NULL, WGPU_STRLEN };

        return instance.CreateSurface(&surfaceDescriptor);
    }
#endif
#ifdef SDL_PLATFORM_LINUX
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        void *x11_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
        uint64_t x11_window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if (!x11_display || !x11_window) return nullptr;

        wgpu::SurfaceSourceXlibWindow fromXlibWindow;
        fromXlibWindow.sType       = wgpu::SType::SurfaceSourceXlibWindow;
        fromXlibWindow.nextInChain = nullptr;
        fromXlibWindow.display = x11_display;
        fromXlibWindow.window = x11_window;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromXlibWindow;
        surfaceDescriptor.label = { NULL, WGPU_STRLEN };

        return instance.CreateSurface(&surfaceDescriptor);
    }
    else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        void *wayland_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        void *wayland_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
        if (!wayland_display || !wayland_surface) return nullptr;

        wgpu::SurfaceSourceWaylandSurface fromWaylandSurface;
        fromWaylandSurface.sType       = wgpu::SType::SurfaceSourceWaylandSurface;
        fromWaylandSurface.nextInChain = nullptr;
        fromWaylandSurface.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        fromWaylandSurface.surface = wayland_surface;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWaylandSurface;
        surfaceDescriptor.label = { NULL, WGPU_STRLEN };

        return instance.CreateSurface(&surfaceDescriptor);
    }
#endif
#ifdef SDL_PLATFORM_WIN32
    {
        HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (!hwnd) return nullptr;
        HINSTANCE hinstance = GetModuleHandle(NULL);

        wgpu::SurfaceSourceWindowsHWND fromWindowsHWND;
        fromWindowsHWND.nextInChain = nullptr;
        fromWindowsHWND.sType       = wgpu::SType::SurfaceSourceWindowsHWND;
        fromWindowsHWND.hinstance   = hinstance;
        fromWindowsHWND.hwnd        = hwnd;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWindowsHWND;
        surfaceDescriptor.label = { NULL, WGPU_STRLEN };

        return instance.CreateSurface(&surfaceDescriptor);
    }
#endif
#ifdef __EMSCRIPTEN__
    {
        wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector fromCanvasHTMLSelector;
        fromCanvasHTMLSelector.selector   = "canvas";

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector;
        surfaceDescriptor.label = { NULL, WGPU_STRLEN };

        return instance.CreateSurface(&surfaceDescriptor);
    }
#endif

    // TODO: See SDL_syswm.h for other possible enum values!
    SDL_Log("No Video driver found...!");
    return nullptr;
}
