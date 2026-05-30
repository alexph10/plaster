#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/Input.h"
#include "Graphics/VulkanContext.h"
#include "Graphics/Renderer.h"

namespace plaster {

Application::Application()
    : m_window(nullptr), m_vulkanContext(nullptr), m_renderer(nullptr) {
    
    m_window = new Window(2560, 1440, "PlasterEngine");
    m_vulkanContext = new VulkanContext(m_window);
    m_renderer = new Renderer(m_window, m_vulkanContext);
}

Application::~Application() {
    delete m_renderer;
    delete m_vulkanContext;
    delete m_window;
}

void Application::run() {
    while (!m_window->shouldClose()) {
        m_window->pollEvents();
        Input::Update();
        m_renderer->render();
    }
}

} // namespace plaster

