#include <entt/entt.hpp>
#include <iostream>

struct Position {
    float x;
    float y;
};

int main() {
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<Position>(entity, 1.0f, 2.0f);

    auto &pos = registry.get<Position>(entity);
    std::cout << pos.x << ", " << pos.y << '\n';
    return 0;
}