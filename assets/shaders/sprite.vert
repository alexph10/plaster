#version 450

// Plastiboo billboard sprite.
//
// Y-axis (cylindrical) billboarding: the sprite stays upright as the
// player pitches up/down, but rotates around world-Y to face the camera.
// This is the Doom / Wolfenstein / classic Quake imp convention - it
// reads better than spherical billboards because the figure always
// stands up straight in the world.
//
// Input geometry is a unit quad in "sprite local" coords:
//   inLocal.x in [-0.5, 0.5]  -> half-width across the sprite
//   inLocal.y in [ 0.0, 1.0]  -> 0 at feet, 1 at head
//
// Push constants pack the per-frame view*proj, plus the per-sprite
// camera position (used for orientation), world position, size, sector
// light, and fog parameters. They are tightly packed (vec4-aligned)
// because Vulkan only guarantees 128 bytes of push-constant space; the
// in-shader names match the SpritePush struct in SpriteRenderer.cpp.

layout(location = 0) in vec2 inLocal;
layout(location = 1) in vec2 inUV;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 cameraPosLight;    // xyz = camera world pos, w = sprite light
    vec4 worldCenterFogStart;// xyz = sprite feet, w = fogStart
    vec4 sizeFog;           // xy = sprite (w, h), z = fogEnd, w = fogBands
    vec4 fogColorEnable;    // rgb = fog colour, a = enable
} pc;

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vLight;
layout(location = 2) out float vViewZ;

void main() {
    vec3 worldCenter = pc.worldCenterFogStart.xyz;

    vec3 toCam = pc.cameraPosLight.xyz - worldCenter;
    vec2 dirXZ = normalize(toCam.xz + vec2(1e-5, 0.0));

    vec3 right = vec3(dirXZ.y, 0.0, -dirXZ.x);
    vec3 up    = vec3(0.0, 1.0, 0.0);

    vec2 size = pc.sizeFog.xy;
    vec3 worldPos = worldCenter
                  + right * (inLocal.x * size.x)
                  + up    * (inLocal.y * size.y);

    vec4 clip = pc.viewProj * vec4(worldPos, 1.0);
    gl_Position = clip;

    vUV    = inUV;
    vLight = pc.cameraPosLight.w;
    vViewZ = clip.w;
}
