# ═══════════════════════════════════════════════════════════════════════════════
# Photon - Adaptive Hybrid Ray Tracer for Low-End GPU Gaming
# CMake Build System
# ═══════════════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.16)
project(Photon VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ─── Build Type ───────────────────────────────────────────────────────────────

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Photon Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Photon Version: ${PROJECT_VERSION}")

# ─── Compiler Flags ──────────────────────────────────────────────────────────

if(MSVC)
    add_compile_options(/W4 /WX- /O2 /arch:AVX2 /fp:fast)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -O2
        -ffast-math -funroll-loops
    )
    # x86-only SIMD flags
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386")
        add_compile_options(-msse4.2)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
        # add_link_options(-fsanitize=address)
    endif()
endif()

# ─── Source Files ─────────────────────────────────────────────────────────────

set(PHOTON_SOURCES
    src/main.c
    src/renderer.c
    src/bvh.c
    src/ray.c
    src/material.c
    src/camera.c
    src/scene.c
    src/budget.c
    src/denoise.c
    src/probe.c
    src/vulkan_backend.c
    src/math_util.c
    src/memory.c
)

set(PHOTON_HEADERS
    include/photon_types.h
    include/renderer.h
    include/bvh.h
    include/ray.h
    include/material.h
    include/camera.h
    include/scene.h
    include/budget.h
    include/denoise.h
    include/probe.h
    include/vulkan_backend.h
    include/math_util.h
    include/memory.h
)

set(PHOTON_SHADERS
    shaders/bvh_common.glsl
    shaders/gbuffer.vert
    shaders/gbuffer.frag
    shaders/shadow_trace.comp
    shaders/reflection_trace.comp
    shaders/gi_trace.comp
    shaders/denoise_spatial.comp
    shaders/denoise_temporal.comp
    shaders/composite.comp
)

# ─── Executable ───────────────────────────────────────────────────────────────

add_executable(photon ${PHOTON_SOURCES} ${PHOTON_HEADERS})

target_include_directories(photon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ─── Platform Libraries ──────────────────────────────────────────────────────

if(WIN32)
    target_link_libraries(photon PRIVATE)
elseif(UNIX)
    target_link_libraries(photon PRIVATE m pthread)
endif()

# ─── Optional: GLFW ──────────────────────────────────────────────────────────

find_package(glfw3 QUIET)
if(glfw3_FOUND)
    target_compile_definitions(photon PRIVATE PHOTON_HAS_GLFW)
    target_link_libraries(photon PRIVATE glfw)
    message(STATUS "GLFW found - using real windowing")
else()
    message(STATUS "GLFW not found - using stub windowing (headless mode)")
endif()

# ─── Optional: Vulkan SDK ────────────────────────────────────────────────────

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_compile_definitions(photon PRIVATE PHOTON_HAS_VULKAN)
    target_include_directories(photon PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(photon PRIVATE Vulkan::Vulkan)
    message(STATUS "Vulkan SDK found - using real Vulkan backend")
else()
    message(STATUS "Vulkan SDK not found - using stub GPU backend")
endif()

# ─── Shader Compilation (SPIR-V) ─────────────────────────────────────────────

find_program(GLSLC glslangValidator)
if(NOT GLSLC)
    find_program(GLSLC glslc)
endif()

if(GLSLC)
    set(SPIRV_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

    foreach(SHADER ${PHOTON_SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        # Skip .glsl includes (they're #included by other shaders)
        get_filename_component(SHADER_EXT ${SHADER} LAST_EXT)
        if(NOT SHADER_EXT STREQUAL ".glsl")
            set(SPIRV_FILE ${SPIRV_OUTPUT_DIR}/${SHADER_NAME}.spv)
            add_custom_command(
                OUTPUT ${SPIRV_FILE}
                COMMAND ${GLSLC} -V ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPIRV_FILE}
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
                COMMENT "Compiling shader: ${SHADER_NAME}"
            )
            list(APPEND SPIRV_FILES ${SPIRV_FILE})
        endif()
    endforeach()

    add_custom_target(shaders ALL DEPENDS ${SPIRV_FILES})
    add_dependencies(photon shaders)
    message(STATUS "Shader compiler found - SPIR-V compilation enabled")
else()
    message(STATUS "No shader compiler found - shaders will not be compiled")
endif()

# ─── Copy Shader Sources to Build Dir ────────────────────────────────────────

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})

# ─── Copy Config Files ───────────────────────────────────────────────────────

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR})
endif()

# ─── Install ──────────────────────────────────────────────────────────────────

install(TARGETS photon RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION share/photon/shaders)
install(DIRECTORY config/ DESTINATION share/photon/config OPTIONAL)

# ─── Summary ──────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════")
message(STATUS "  Photon Renderer v${PROJECT_VERSION}")
message(STATUS "  Build:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  GLFW:     ${glfw3_FOUND}")
message(STATUS "  Vulkan:   ${Vulkan_FOUND}")
message(STATUS "  Shaders:  ${GLSLC}")
message(STATUS "═══════════════════════════════════════════════")
message(STATUS "")
