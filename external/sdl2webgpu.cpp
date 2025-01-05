#include "sdl2webgpu.h"

#include <webgpu/webgpu.h>

#ifdef SDL_VIDEO_DRIVER_COCOA
    #include <Cocoa/Cocoa.h>
    #include <Foundation/Foundation.h>
    #include <QuartzCore/CAMetalLayer.h>
#endif
#ifdef SDL_VIDEO_DRIVER_UIKIT
    #include <Foundation/Foundation.h>
    #include <Metal/Metal.h>
    #include <QuartzCore/CAMetalLayer.h>
    #include <UIKit/UIKit.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <emscripten/html5_webgpu.h>
#endif

WGPUSurface SDL_GetWGPUSurface(WGPUInstance instance, SDL_Window* window)
{
    SDL_SysWMinfo windowWMInfo;
    SDL_VERSION(&windowWMInfo.version);
    SDL_GetWindowWMInfo(window, &windowWMInfo);

#ifdef SDL_VIDEO_DRIVER_COCOA
    {
        id metal_layer      = NULL;
        NSWindow* ns_window = windowWMInfo.info.cocoa.window;
        [ns_window.contentView setWantsLayer:YES];
        metal_layer = [CAMetalLayer layer];
        [ns_window.contentView setLayer:metal_layer];

        WGPUSurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;

        fromMetalLayer.chain.next = NULL;
        fromMetalLayer.layer      = metal_layer;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;

        WGPUStringView view;
        view.data               = "SDL2 WebGPU Surface";
        view.length             = 19;
        surfaceDescriptor.label = view;

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#endif
#ifdef SDL_VIDEO_DRIVER_UIKIT
    {
        UIWindow* ui_window       = windowWMInfo.info.uikit.window;
        UIView* ui_view           = ui_window.rootViewController.view;
        CAMetalLayer* metal_layer = [CAMetalLayer new];
        metal_layer.opaque        = true;
        metal_layer.frame         = ui_view.frame;
        metal_layer.drawableSize  = ui_view.frame.size;

        [ui_view.layer addSublayer:metal_layer];

        WGPUSurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        fromMetalLayer.chain.next  = NULL;
        fromMetalLayer.layer       = metal_layer;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;

        WGPUStringView view;
        view.data               = "SDL2 WebGPU Surface";
        view.length             = 19;
        surfaceDescriptor.label = view;

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#endif
#ifdef SDL_VIDEO_DRIVER_X11
    {
        Display* x11_display = windowWMInfo.info.x11.display;
        Window x11_window    = windowWMInfo.info.x11.window;

        WGPUSurfaceSourceXlibWindow fromXlibWindow;
        fromXlibWindow.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        fromXlibWindow.chain.next  = NULL;
        fromXlibWindow.display     = x11_display;
        fromXlibWindow.window      = x11_window;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromXlibWindow.chain;

        WGPUStringView view;
        view.data               = "SDL2 WebGPU Surface";
        view.length             = 19;
        surfaceDescriptor.label = view;

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#endif
#ifdef SDL_VIDEO_DRIVER_WAYLAND
    {
        struct wl_display* wayland_display = windowWMInfo.info.wl.display;
        struct wl_surface* wayland_surface = windowWMInfo.info.wl.surface;

        WGPUSurfaceSourceWaylandSurface fromWaylandSurface;
        fromWaylandSurface.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        fromWaylandSurface.chain.next  = NULL;
        fromWaylandSurface.display     = wayland_display;
        fromWaylandSurface.surface     = wayland_surface;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWaylandSurface.chain;

        WGPUStringView view;
        view.data               = "SDL2 WebGPU Surface";
        view.length             = 19;
        surfaceDescriptor.label = view;

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#endif
#ifdef SDL_VIDEO_DRIVER_WINDOWS
    {
        HWND hwnd           = windowWMInfo.info.win.window;
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WGPUSurfaceSourceWindowsHWND fromWindowsHWND;
        fromWindowsHWND.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        fromWindowsHWND.chain.next  = NULL;
        fromWindowsHWND.hinstance   = hinstance;
        fromWindowsHWND.hwnd        = hwnd;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWindowsHWND.chain;

        WGPUStringView view;
        view.data               = "SDL2 WebGPU Surface";
        view.length             = 19;
        surfaceDescriptor.label = view;

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#endif
#ifdef SDL_VIDEO_DRIVER_EMSCRIPTEN
    {
        WGPUSurfaceDescriptorFromCanvasHTMLSelector fromCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;

        fromCanvasHTMLSelector.chain.next = NULL;
        fromCanvasHTMLSelector.selector   = "canvas";

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector.chain;
        surfaceDescriptor.label       = NULL;

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#endif

    // TODO: See SDL_syswm.h for other possible enum values!
    SDL_Log("No Video driver found...!");
    return nullptr;
}
