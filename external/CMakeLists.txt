add_library(sdl2webgpu STATIC sdl2webgpu.c)
target_include_directories(sdl2webgpu PUBLIC .)

if (NOT EMSCRIPTEN)
    target_link_libraries(sdl2webgpu PUBLIC SDL2::SDL2 dawn::webgpu_dawn)
endif()

if (APPLE)
    target_compile_options(sdl2webgpu PRIVATE -x objective-c)
    target_link_libraries(sdl2webgpu PRIVATE "-framework CoreVideo" "-framework IOKit" "-framework QuartzCore")

    if (IOS)
        target_link_libraries(sdl2webgpu PRIVATE "-framework UIKit")
    else()
        target_link_libraries(sdl2webgpu PRIVATE "-framework Cocoa")
    endif()
endif()
