add_library(sdl3webgpu STATIC sdl3webgpu.mm)
target_include_directories(sdl3webgpu PUBLIC .)

if (EMSCRIPTEN)
    target_link_libraries(sdl3webgpu PUBLIC SDL3::SDL3 dawn::webgpu_dawn)
else()
    target_link_libraries(sdl3webgpu PUBLIC SDL3::SDL3 dawn::webgpu_dawn)
endif()

if (APPLE)
    target_link_libraries(sdl3webgpu PRIVATE "-framework CoreVideo" "-framework IOKit" "-framework QuartzCore")

    if (IOS)
        target_link_libraries(sdl3webgpu PRIVATE "-framework UIKit")
    else()
        target_link_libraries(sdl3webgpu PRIVATE "-framework Cocoa")
    endif()
endif()
