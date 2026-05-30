cmake_minimum_required(VERSION 3.20)
project(plasterEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Dependencies
find_package(Vulkan REQUIRED)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)
add_subdirectory(external/glm)

# Dear ImGui (no upstream CMake)
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
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

# Engine library
add_library(plasterEngine STATIC
    src/Core/Application.cpp
    src/Core/Window.cpp
    src/Core/Input.cpp
    src/Graphics/Renderer.cpp
    src/Graphics/Vulkan/VulkanContext.cpp
    src/Graphics/UI/ImGuiManager.cpp
)
target_include_directories(plasterEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(plasterEngine PUBLIC Vulkan::Vulkan glfw glm::glm imgui)

# Executable
add_executable(plasterEngine_app src/main.cpp)
target_link_libraries(plasterEngine_app PRIVATE plasterEngine)

# Warnings
if(MSVC)
    target_compile_options(plasterEngine     PRIVATE /W4)
    target_compile_options(plasterEngine_app PRIVATE /W4)
else()
    target_compile_options(plasterEngine     PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(plasterEngine_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

