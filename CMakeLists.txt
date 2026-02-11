cmake_minimum_required(VERSION 3.19)

if (APPLE AND DEFINED ENV{RPATH})
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH $ENV{RPATH})
endif()

if (DEFINED CMAKE_TOOLCHAIN_FILE)
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_TOOLCHAIN_FILE})
endif()

if (DEFINED ENV{VCPKG_ROOT})
    cmake_path(SET VCPKG_PATH NORMALIZE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
    set(CMAKE_TOOLCHAIN_FILE ${VCPKG_PATH})
else()
    message(FATAL_ERROR "Please install vcpkg and set 'VCPKG_ROOT' environmental variable")    
endif()

project(main VERSION 1.0)

if (EMSCRIPTEN)
    set(VCPKG_TARGET_TRIPLET "wasm32-emscripten")
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(Dawn REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(imgui CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>")

add_subdirectory(external sdl3webgpu)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(
    main
    ${SOURCES}
)

if (EMSCRIPTEN)

    target_link_options(
        main PRIVATE
        -sASYNCIFY
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        --preload-file resources/
    )

    set_target_properties(main PROPERTIES SUFFIX ".js")

    file(COPY ${PROJECT_SOURCE_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
    file(COPY "index.html" DESTINATION ${CMAKE_BINARY_DIR})

    target_link_libraries(
        main PRIVATE
        SDL3::SDL3
        dawn::webgpu_dawn
        glm::glm
        tinyobjloader::tinyobjloader
        imgui::imgui
        sdl3webgpu
    )

    target_include_directories(main PRIVATE ${Stb_INCLUDE_DIR})

else()

    target_link_libraries(
        main PRIVATE
        dawn::webgpu_dawn
        SDL3::SDL3
        glm::glm
        tinyobjloader::tinyobjloader
        imgui::imgui
        sdl3webgpu
    )

    target_include_directories(main PRIVATE ${Stb_INCLUDE_DIR})

    add_custom_command(
        TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/resources $<TARGET_FILE_DIR:main>/resources
    )

endif()
