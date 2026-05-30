#include "Core/Input.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <ppltasks.h>

namespace plaster {
Input::State Input::s_currentState = {};
Input::State Input::s_previousState = {};
GLFWwindow* Input::s_window = nullptr;

void Input::Init(GLFWwindow* window) {
  s_window = window;

  glfwSetKeyCallback(window, KeyCallback);
  glfwSetMouseButtonCallback(window, MouseButtonCallback);
  glfwSetScrollCallback(window, MouseScrollCallback);
  glfwSetCursorPosCallback(window, CursorPosCallback);
}

void Input::Update() {
  s_previousState = s_currentState;
  
  std::memset(s_currentState.keysPressed, 0 ,sizeof(s_currentState.keysPressed));
  std::memset(s_currentState.keysReleased, 0, sizeof(s_currentState.keysReleased));
  std::memset(s_currentState.mousePressed, 0, sizeof(s_currentState.mousePressed));
  std::memset(s_currentState.mouseReleased, 0, sizeof(s_currentState.mouseReleased));

  
  s_currentState.scrollX = 0.0f;
  s_currentState.scrollY = 0.0f;
}

bool Input::IsKeyPressed(Key key) {
  int keycode = static_cast<int>(key);
  return keycode >= 0 && keycode < 512 && s_currentState.keysPressed[keycode];
}
bool Input::IsKeyDown(Key key) {
  int keycode = static_cast<int>(key);
  return keycode >= 0 && keycode < 512 && s_currentState.keysDown[keycode];
}

bool Input::IsKeyReleased(Key key) {
  int keycode = static_cast<int>(key);
  return keycode >= 0 && keycode < 512 && s_currentState.keysReleased[keycode];
}
bool Input::IsMouseButtonPressed(MouseButton button) {
  int btn = static_cast<int>(button);
  return btn >= 0 && btn < 8 && s_currentState.mousePressed[btn];
}
bool Input::IsMouseButtonDown(MouseButton button) {
  int btn = static_cast<int>(button);
  return btn >= 0 && btn < 8 && s_currentState.mouseDown[btn];
}

bool Input::IsMouseButtonReleased(MouseButton button) {
  int btn = static_cast<int>(button);
  return btn >= 0 && btn < 8 && s_currentState.mouseReleased[btn];
}

float Input::GetMouseX() {
  return s_currentState.mouseX;
}

float Input::GetMouseY() {
  return s_currentState.mouseY;
}

std::pair<float, float> Input::GetMousePosition() {
  return { s_currentState.mouseX, s_currentState.mouseY };
}

float Input::GetMouseScrollX() {
  return s_currentState.scrollX;
}
float Input::GetMouseScrollY() {
  return s_currentState.scrollY;
}

void Input::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (key < 0 || key >= 512) return;
  if (action == GLFW_PRESS) {
    s_currentState.keysPressed[key] = true;
    s_currentState.keysDown[key] = true;
  } else if (action == GLFW_RELEASE) {
    s_currentState.keysReleased[key] = true;
    s_currentState.keysDown[key] = false;
  }
}

void Input::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
  if (button < 0 || button >= 8) return;
  
  if (action == GLFW_PRESS) {
    s_currentState.mousePressed[button] = true;
    s_currentState.mouseDown[button] = true;
  } else if (action == GLFW_RELEASE) {
    s_currentState.mouseReleased[button] = true;
    s_currentState.mouseDown[button] = false;
  }
}

void Input::MouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
  s_currentState.scrollX += static_cast<float>(xoffset);
  s_currentState.scrollY += static_cast<float>(yoffset);
}
void Input::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
  s_currentState.mouseX = static_cast<float>(xpos);
  s_currentState.mouseY = static_cast<float>(ypos);
}

}
