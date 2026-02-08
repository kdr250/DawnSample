add_library(sdl2webgpu STATIC sdl2webgpu.mm imgui_impl_wgpu.cpp imgui_impl_sdl2.h imgui_impl_sdl2.cpp)
target_include_directories(sdl2webgpu PUBLIC .)

if (EMSCRIPTEN)
    target_link_libraries(sdl2webgpu PUBLIC SDL2::SDL2 dawn::webgpu_dawn imgui::imgui)
else()
    add_compile_definitions(IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    target_link_libraries(sdl2webgpu PUBLIC SDL2::SDL2 dawn::webgpu_dawn imgui::imgui)
endif()

if (APPLE)
    target_link_libraries(sdl2webgpu PRIVATE "-framework CoreVideo" "-framework IOKit" "-framework QuartzCore")

    if (IOS)
        target_link_libraries(sdl2webgpu PRIVATE "-framework UIKit")
    else()
        target_link_libraries(sdl2webgpu PRIVATE "-framework Cocoa")
    endif()
endif()
