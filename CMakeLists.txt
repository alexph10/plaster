cmake_minimum_required(VERSION 3.20)
project(plaster CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()


# Dependencies
find_package(Vulkan REQUIRED)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)
add_subdirectory(external/glm)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

include(FetchContent)
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.2
)
FetchContent_MakeAvailable(entt)


# Engine executable

add_executable(plaster
    src/main.cpp

    # Core
    src/Core/Application.cpp
    src/Core/Window.cpp
    src/Core/Input.cpp
    src/Core/FileSystem.cpp

    # Graphics
    src/Graphics/Renderer.cpp
    src/Graphics/CubeRenderer.cpp
    src/Graphics/GridMap.cpp
    src/Graphics/MapRenderer.cpp
    src/Graphics/PostProcessor.cpp
    src/Graphics/SpriteRenderer.cpp
    src/Graphics/Vulkan/VulkanContext.cpp
    src/Graphics/Vulkan/VulkanUtils.cpp
    src/Graphics/UI/ImGuiManager.cpp

    # Scene 3D
    src/Scene/3D/Core/scene.cpp
    src/Scene/3D/Core/entity.cpp
    src/Scene/3D/Components/identity.cpp
    src/Scene/3D/Components/transform.cpp
    src/Scene/3D/Components/camera.cpp
    src/Scene/3D/Components/render.cpp
    src/Scene/3D/Systems/transform-system.cpp
    src/Scene/3D/Systems/camera-system.cpp
    src/Scene/3D/Systems/render-system.cpp
    src/Scene/3D/Systems/first-person-camera-system.cpp
)

target_include_directories(plaster PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(plaster PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    imgui
    EnTT::EnTT
)

# GLM coordinate-space defines applied globally so every TU agrees:
#  - Vulkan uses [0, 1] depth (not OpenGL's [-1, 1])
#  - GLM trig defaults to radians; making it explicit avoids surprises
target_compile_definitions(plaster PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
  target_compile_options(plaster PRIVATE /W4)
else()
  target_compile_options(plaster PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Shader compilation: glslc *.vert/*.frag -> *.spv, then copy next to exe
# ---------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS
      $ENV{VULKAN_SDK}/Bin
      $ENV{VULKAN_SDK}/bin
)
if(NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or set VULKAN_SDK.")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
set(SHADER_BINARY_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADER_SOURCES
    cube.vert
    cube.frag
    fullscreen.vert
    post_dither.frag
    sprite.vert
    sprite.frag
)

set(SHADER_SPV_OUTPUTS "")
foreach(SHADER ${SHADER_SOURCES})
    set(SHADER_IN  ${SHADER_SOURCE_DIR}/${SHADER})
    set(SHADER_OUT ${SHADER_BINARY_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT  ${SHADER_OUT}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_IN} -o ${SHADER_OUT}
        DEPENDS ${SHADER_IN}
        COMMENT "glslc ${SHADER} -> ${SHADER}.spv"
        VERBATIM
    )
    list(APPEND SHADER_SPV_OUTPUTS ${SHADER_OUT})
endforeach()

add_custom_target(plaster_shaders ALL DEPENDS ${SHADER_SPV_OUTPUTS})
add_dependencies(plaster plaster_shaders)

# Stage the SPV files next to the executable so runtime asset lookup
# (Plaster::GetExecutableDirectory() / "shaders") finds them in every
# build configuration (Debug/Release/RelWithDebInfo).
add_custom_command(TARGET plaster POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:plaster>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADER_BINARY_DIR}
            $<TARGET_FILE_DIR:plaster>/shaders
    COMMENT "Copying compiled shaders next to plaster executable"
)
