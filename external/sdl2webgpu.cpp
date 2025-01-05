#include "sdl2webgpu.h"

#include <webgpu/webgpu_cpp.h>

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

wgpu::Surface SDL_GetWGPUSurface(wgpu::Instance instance, SDL_Window* window)
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

        wgpu::SurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.sType = wgpu::SType_SurfaceSourceMetalLayer;

        fromMetalLayer.chain.next = NULL;
        fromMetalLayer.layer      = metal_layer;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;

        wgpu::StringView view;
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

        wgpu::SurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.sType = wgpu::SType_SurfaceSourceMetalLayer;
        fromMetalLayer.chain.next  = NULL;
        fromMetalLayer.layer       = metal_layer;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;

        wgpu::StringView view;
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

        wgpu::SurfaceSourceXlibWindow fromXlibWindow;
        fromXlibWindow.chain.sType = wgpu::SType_SurfaceSourceXlibWindow;
        fromXlibWindow.chain.next  = NULL;
        fromXlibWindow.display     = x11_display;
        fromXlibWindow.window      = x11_window;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromXlibWindow.chain;

        wgpu::StringView view;
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

        wgpu::SurfaceSourceWaylandSurface fromWaylandSurface;
        fromWaylandSurface.chain.sType = wgpu::SType_SurfaceSourceWaylandSurface;
        fromWaylandSurface.chain.next  = NULL;
        fromWaylandSurface.display     = wayland_display;
        fromWaylandSurface.surface     = wayland_surface;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWaylandSurface.chain;

        wgpu::StringView view;
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

        wgpu::SurfaceSourceWindowsHWND fromWindowsHWND;
        fromWindowsHWND.nextInChain = nullptr;
        fromWindowsHWND.sType       = wgpu::SType::SurfaceSourceWindowsHWND;
        fromWindowsHWND.hinstance   = hinstance;
        fromWindowsHWND.hwnd        = hwnd;

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWindowsHWND;

        wgpu::StringView view;
        view.data               = "SDL2 WebGPU Surface";
        view.length             = 19;
        surfaceDescriptor.label = view;

        return instance.CreateSurface(&surfaceDescriptor);
    }
#endif
#ifdef SDL_VIDEO_DRIVER_EMSCRIPTEN
    {
        wgpu::SurfaceDescriptorFromCanvasHTMLSelector fromCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.sType = wgpu::SType_SurfaceDescriptorFromCanvasHTMLSelector;

        fromCanvasHTMLSelector.chain.next = NULL;
        fromCanvasHTMLSelector.selector   = "canvas";

        wgpu::SurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector.chain;
        surfaceDescriptor.label       = NULL;

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#endif

    // TODO: See SDL_syswm.h for other possible enum values!
    SDL_Log("No Video driver found...!");
    return nullptr;
}
