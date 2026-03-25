cmake_minimum_required(VERSION 3.20)
project(triangle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wframe-larger-than=1048576 -Werror=frame-larger-than=1048576")

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

# ── GLM ───────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# ── Shader compilation ───────────────────────────────────────────────────────
find_program(GLSLC glslc REQUIRED)

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_BINARY_DIR}/shaders)

file(GLOB SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/*.vert
    ${SHADER_SOURCE_DIR}/*.frag
)

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BINARY_DIR}
        COMMAND ${GLSLC} -I${CMAKE_SOURCE_DIR}/src ${SHADER} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER} ${CMAKE_SOURCE_DIR}/src/shared.h
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_BINARIES ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_BINARIES})

# ── executable ────────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME} src/main.cc src/util.cc)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3-static
    volk
    Vulkan::Headers
    glm::glm
)

add_dependencies(${PROJECT_NAME} shaders)

# ── macOS: MoltenVK ───────────────────────────────────────────────────────────
if(APPLE)
    # LunarG SDK takes priority, fall back to Homebrew
    if(DEFINED ENV{VULKAN_SDK})
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "$ENV{VULKAN_SDK}/lib/libMoltenVK.dylib"
        )
    else()
        # arm64 Homebrew prefix; Intel is /usr/local
        set(HOMEBREW_PREFIX "/opt/homebrew" CACHE PATH "Homebrew prefix")
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${HOMEBREW_PREFIX}/lib/libMoltenVK.dylib"
        )
    endif()
endif()