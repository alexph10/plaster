#pragma once

#include "KeyCodes.h"
#include <utility>

struct GLFWwindow;

namespace plaster {
class Input {
public:
  static bool IsKeyPressed(Key key);
  static bool IsKeyDown(Key key);
  static bool IsKeyReleased(Key key);

  static bool IsMouseButtonPressed(MouseButton button);
  static bool IsMouseButtonDown(MouseButton button);
  static bool IsMouseButtonReleased(MouseButton button);

  static float GetMouseX();
  static float GetMouseY();

  static std::pair<float, float> GetMousePosition();

  static float GetMouseScrollX();
  static float GetMouseScrollY();
  
  static void Init(GLFWwindow* window);
  static void Update();

private:
  Input() = delete;

  static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
  static void MouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
  static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
  
  struct State {
    bool keysPressed[512] = {};
    bool keysDown[512] = {};
    bool keysReleased[512] = {};

    bool mousePressed[8] = {};
    bool mouseDown[8] = {};
    bool mouseReleased[8] = {};
    
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
  };

  static State s_currentState;
  static State s_previousState;
  static GLFWwindow* s_window;

  friend class Window;
};

}
