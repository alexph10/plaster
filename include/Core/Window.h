#pragma once

#include <string>
#include <cstdint>

struct GLFWwindow;

namespace plaster {

class Window {
public:
  Window(uint32_t width, uint32_t height, const std::string& title);
  ~Window();
  
  bool shouldClose() const;
  void pollEvents();
  void waitEvents();

  GLFWwindow* getHandle() const { return m_window; }
  uint32_t getWidth() const { return m_width; }
  uint32_t getHeight() const { return m_height; }
  
  void toggleFullscreen();
  bool isFullscreen() const { return m_isFullscreen; }

  // Lock or release the OS cursor. When captured the cursor is hidden and
  // mouse delta becomes the only way to know its motion (used for FPS look).
  // When released the cursor returns to normal and is free to interact with
  // ImGui / window decorations.
  void setCursorCaptured(bool captured);
  bool isCursorCaptured() const { return m_cursorCaptured; }

private:
  GLFWwindow* m_window;
  uint32_t m_width;
  uint32_t m_height;
  bool m_isFullscreen = false;
  bool m_cursorCaptured = false;
};

} // namespace plaster

