#pragma once

#include <glm/glm.hpp>

namespace plaster {

// Engine-wide stylized fog state. Lives on the Renderer; passed by const
// reference into each subsystem's record() so cube, map, and sprite
// geometry all attenuate consistently with view-space distance.
//
// Banded fog is the Plastiboo / Doom signature: instead of a smooth
// linear interpolation toward the fog colour (which produces ugly value
// banding once the palette quantize hits it), we *intentionally*
// quantize the fog factor into N discrete steps before mixing. That
// keeps depth darkening on the same visual register as the palette ramp
// and reads as deliberate art direction rather than a hardware
// limitation.
struct FogParams {
    glm::vec3 color  {0.05f, 0.02f, 0.0f}; // warm ember shadow by default
    float     start  {6.0f};   // metres from camera where fog begins
    float     end    {28.0f};  // metres from camera where fog reaches color
    float     bands  {5.0f};   // # of discrete fog steps; matches palette band count
    bool      enable {true};
};

} // namespace plaster
