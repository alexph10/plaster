#include "Core/Application.h"
#include <iostream>
#include <exception>

int main() {
  try {
    plaster::Application app;
    app.run();
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    std::cerr << "Press Enter to exit..." << std::endl;
    std::cin.get();
    return -1;
  }
  return 0;
}

