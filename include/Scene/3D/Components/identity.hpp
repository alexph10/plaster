// UUID (Universally Unique Identifier) is a persistent identifier used to
// distinguish one entity from every other entity, even across saves, scene
// copies, and serialization boundaries. Unlike transient ECS handles such as
// entt::entity, a UUID is meant to remain stable over time, which makes it
// useful for editor tooling, asset references, prefab workflows, and restoring
// entity relationships after loading data back from disk.

#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <string_view>

namespace plaster::scene3d {

class UUID {
public:
  UUID() : m_value(Generate()) {}

  UUID(std::uint64_t value) : m_value(value) {}

  UUID(const UUID &) = default;
  UUID &operator=(const UUID &) = default;

  operator std::uint64_t() const { return m_value; }

  std::uint64_t Value() const { return m_value; }

  bool IsValid() const { return m_value != 0; }

  bool operator==(const UUID &other) const { return m_value == other.m_value; }
  bool operator!=(const UUID &other) const { return !(*this == other); }
  bool operator<(const UUID &other) const { return m_value < other.m_value; }

private:
  static std::uint64_t Generate() {
    static std::random_device randomDevice;
    static std::mt19937_64 engine(randomDevice());
    static std::uniform_int_distribution<std::uint64_t> distribution;

    std::uint64_t value = 0;
    while (value == 0)
      value = distribution(engine);

    return value;
  }

private:
    std::uint64_t m_value = 0;
};

struct IDComponent {
  UUID ID{};

  IDComponent() = default;
  IDComponent(const UUID &id)
      : ID(id) {}
};

struct NameComponent {
  std::string Name{"Entity"};

  NameComponent() = default;
  NameComponent(const std::string &name) : Name(name) {}
  NameComponent(std::string &&name) : Name(std::move(name)) {}
};

struct LayerComponent {
  std::string Layer{"Default"};

  LayerComponent() = default;
  LayerComponent(const std::string &layer) : Layer(layer) {}
  LayerComponent(std::string &&layer) : Layer(std::move(layer)) {}
};

struct SelectionComponent {
    bool Selected = false;
};

struct EditorMetadataComponent {
  bool VisibleInHierarchy = true;
  bool VisibleInInspector = true;
  bool Serializable = true;
  bool Locked = false;
  bool PrefabInstance = false;
};

} // namespace plaster::scene3d

namespace std {
template <> struct hash<plaster::scene3d::UUID> {
  std::size_t operator()(const plaster::scene3d::UUID &uuid) const {
      return hash<std::uint64_t>()(static_cast<std::uint64_t>(uuid));
    }
};
}
