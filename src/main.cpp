#include "Core/Application.h"

#include <cstdio>
#include <exception>

int main() {
    try {
        plaster::Application app;
        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "plaster: fatal error: %s\n", e.what());
        return 1;
    }
    return 0;
}
