cmake_minimum_required(VERSION 3.20)

find_program(DEFAULT_CLANG clang)
find_program(DEFAULT_CLANGXX clang++)

if(NOT DEFINED CMAKE_C_COMPILER AND DEFAULT_CLANG)
    set(CMAKE_C_COMPILER "${DEFAULT_CLANG}" CACHE FILEPATH "Default C compiler" FORCE)
endif()

if(NOT DEFINED CMAKE_CXX_COMPILER AND DEFAULT_CLANGXX)
    set(CMAKE_CXX_COMPILER "${DEFAULT_CLANGXX}" CACHE FILEPATH "Default CXX compiler" FORCE)
endif()

project(triangle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-O3 -march=native -g)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wframe-larger-than=1048576 -Werror=frame-larger-than=1048576)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wframe-larger-than=1048576)
endif()

include(FetchContent)

# ── SDL3 ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.10
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# ── Vulkan Headers ────────────────────────────────────────────────────────────
FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(VulkanHeaders)

# ── volk ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(volk)
target_link_libraries(volk PUBLIC Vulkan::Headers)

# ── Shader compilation ───────────────────────────────────────────────────────
set(SLANGC "" CACHE FILEPATH "Path to the slangc executable")

if(SLANGC AND NOT EXISTS "${SLANGC}")
    message(FATAL_ERROR "SLANGC points to a missing file: ${SLANGC}")
endif()

if(NOT SLANGC)
    find_package(slang CONFIG QUIET)
    if(slang_FOUND AND DEFINED SLANG_EXECUTABLE AND EXISTS "${SLANG_EXECUTABLE}")
        set(SLANGC "${SLANG_EXECUTABLE}")
    endif()
endif()

if(NOT SLANGC)
    set(_slang_hints)
    if(DEFINED ENV{SLANG_DIR})
        list(APPEND _slang_hints "$ENV{SLANG_DIR}" "$ENV{SLANG_DIR}/bin")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _slang_hints "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
    endif()

    find_program(SLANGC
        NAMES slangc slangc.exe
        HINTS ${_slang_hints}
        PATH_SUFFIXES bin
    )
endif()

if(NOT SLANGC)
    message(FATAL_ERROR
        "Could not find slangc. Supported setup paths are: "
        "1) a Slang install discoverable via find_package(slang CONFIG), "
        "2) the Vulkan SDK (1.3.296.0 or newer ships slangc), or "
        "3) slangc on PATH / passed with -DSLANGC=/path/to/slangc."
    )
endif()

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_BINARY_DIR}/shaders)

function(add_slang_shader_module SHADER_NAME ENTRY_POINT SHADER_STAGE OUTPUT_NAME)
    set(SHADER ${SHADER_SOURCE_DIR}/${SHADER_NAME}.slang)
    set(SHADER_OUTPUT ${SHADER_BINARY_DIR}/${OUTPUT_NAME})
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BINARY_DIR}
        COMMAND ${SLANGC}
                -lang slang
                -target spirv
                -profile glsl_450
                -entry ${ENTRY_POINT}
                -stage ${SHADER_STAGE}
                # Match the existing GLM/GLSL-side push constant matrix layout.
                -matrix-layout-column-major
                -I ${SHADER_SOURCE_DIR}
                ${SHADER}
                -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER} ${SHADER_SOURCE_DIR}/shared.h ${CMAKE_SOURCE_DIR}/src/util.h
        COMMENT "Compiling Slang shader ${OUTPUT_NAME}"
    )
    list(APPEND SHADER_BINARIES ${SHADER_OUTPUT})
    set(SHADER_BINARIES ${SHADER_BINARIES} PARENT_SCOPE)
endfunction()

add_slang_shader_module(globe vertexMain   vertex   globe.vert.spv)
add_slang_shader_module(globe fragmentMain fragment globe.frag.spv)
add_slang_shader_module(point vertexMain   vertex   point.vert.spv)
add_slang_shader_module(point fragmentMain fragment point.frag.spv)

add_custom_target(shaders ALL DEPENDS ${SHADER_BINARIES})

# ── executable ────────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME} src/main.cc src/util.cc)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3-static
    volk
    Vulkan::Headers
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/shaders
    ${CMAKE_SOURCE_DIR}/src
)

add_dependencies(${PROJECT_NAME} shaders)
