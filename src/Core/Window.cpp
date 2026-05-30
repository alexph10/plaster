#include "Core/Window.h"
#include "Core/Input.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace plaster {

Window::Window(uint32_t width, uint32_t height, const std::string& title)
    : m_width(width), m_height(height), m_window(nullptr) {
    
    static bool glfwInitialized = false;
    if (!glfwInitialized) {
        if (!glfwInit()) {
            return;
        }
        glfwInitialized = true;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    
    if (!m_window) {
        const char* description;
        glfwGetError(&description);
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
    
    Input::Init(m_window);
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::waitEvents() {
    glfwWaitEvents();
}

void Window::toggleFullscreen() {
    m_isFullscreen = !m_isFullscreen;
    
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    
    if (m_isFullscreen) {
        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(m_window, nullptr, 100, 100, 1920, 1080, 0);
    }
}

} // namespace plaster

