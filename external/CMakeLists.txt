add_library(sdl3webgpu STATIC sdl3webgpu.mm imgui_impl_wgpu.cpp)
target_include_directories(sdl3webgpu PUBLIC .)

if (EMSCRIPTEN)
    target_link_libraries(sdl3webgpu PUBLIC SDL3::SDL3 dawn::webgpu_dawn imgui::imgui)
else()
    add_compile_definitions(IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
    target_link_libraries(sdl3webgpu PUBLIC SDL3::SDL3 dawn::webgpu_dawn imgui::imgui)
endif()

if (APPLE)
    target_link_libraries(sdl3webgpu PRIVATE "-framework CoreVideo" "-framework IOKit" "-framework QuartzCore")

    if (IOS)
        target_link_libraries(sdl3webgpu PRIVATE "-framework UIKit")
    else()
        target_link_libraries(sdl3webgpu PRIVATE "-framework Cocoa")
    endif()
endif()
