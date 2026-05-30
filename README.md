# Plaster

A small C++17 3D game engine aimed at rasterized, low-resolution, **pixelated graphics**
inspired by the Plastiboo / PS1-horror aesthetic (color quantization, dithering,
vertex snap, grunge overlay, palette swaps).

> Status: **early bootstrap.** Opens a window, brings up a Vulkan device + swapchain,
> renders an ImGui overlay. No geometry pipeline yet — that's the next milestone.

## Layout

```
plaster/
├── src/
│   ├── main.cpp
│   ├── Core/                     Application, Window, Input (GLFW)
│   └── Graphics/
│       ├── Renderer.cpp          frame loop, swapchain, render pass
│       ├── Vulkan/               low-level VK objects (Context, future Pipeline/Buffer)
│       └── UI/                   ImGuiManager
├── include/                      public headers, mirrors src/ layout
├── docs/
│   └── PLASTIBOO_STYLE.md        target visual style + rendering plan
├── external/                     git submodules: glfw, glm, imgui (docking)
├── assets/                       (empty, for future textures/models)
├── CMakeLists.txt
└── build.bat                     one-shot configure + build + run (Windows)
```

## Build

### Prerequisites

- CMake ≥ 3.20
- A C++17 compiler (MSVC 2022 is what `build.bat` targets)
- [Vulkan SDK](https://vulkan.lunarg.com/) installed and on `PATH`

### Clone

```bash
git clone --recurse-submodules https://github.com/alexph10/plaster.git
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

### Build & run (Windows)

```bat
build.bat
```

### Build (any platform)

```bash
cmake -S . -B build
cmake --build build --config Debug
./build/Debug/plasterEngine_app        # path depends on generator
```

## Architecture today

```
                 ╭──────────────────╮
                 │  main.cpp        │
                 ╰────────┬─────────╯
                          ▼
            ╭───────────────────────────╮
            │ Core/Application          │  game loop
            ╰──┬─────────────┬──────────╯
               ▼             ▼
     ╭───────────────╮  ╭────────────────────╮
     │ Core/Window   │  │ Graphics/          │
     │ Core/Input    │  │   VulkanContext    │  instance + device + surface
     ╰───────────────╯  │   Renderer         │  swapchain + render pass
                        │   ImGuiManager     │  debug overlay
                        ╰────────────────────╯
```

## Roadmap

1. [x] Vulkan instance, device, swapchain, ImGui overlay
2. [ ] Graphics pipeline + textured spinning cube (vertex/index buffers, depth, MVP)
3. [ ] Render to low-res offscreen image (e.g. 320×240), nearest-blit to swapchain
4. [ ] Post-process pass: palette quantization + ordered/blue-noise dithering
5. [ ] Minimal scene representation (`Transform` + `Mesh` + `Material`)
6. [ ] Free-fly camera bound to `Input`
7. [ ] Swapchain recreation on window resize

## License

See [LICENSE](LICENSE).
