#version 450

// Plastiboo vertex shader, used by both CubeRenderer and MapRenderer.
//
// Deliberate artifacts:
//   1. PS1-style vertex jitter: after projection, clip-space XY is snapped
//      to the low-resolution offscreen pixel grid. Geometry edges visibly
//      "step" when the camera moves - old hardware lacked sub-pixel
//      precision and we mimic that signature.
//   2. Affine UV interpolation: the UV varying is declared `noperspective`
//      so the rasterizer skips perspective correction. Camera-facing
//      edges visibly swim and skew - the classic PS1 look.
//
// Lighting and fog data:
//   - inLight (0..1) is a per-vertex baked light level coming from the
//     map's per-cell light field; the cube ships 1.0 everywhere.
//   - vViewZ forwards the perspective w to the fragment shader, which
//     equals view-space -Z (camera-forward distance) - that's the value
//     we feed into the stylized banded fog.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in float inLight;

layout(push_constant) uniform PushConstants {
    mat4 mvp;              // model-view-projection
    vec4 lowResSize;       // xy = offscreen pixel size; zw padding
    vec4 fogColorEnable;   // rgb = fog colour;  a = 0/1 toggle
    vec4 fogParams;        // x = start, y = end, z = bands, w pad
} pc;

layout(location = 0) noperspective out vec2 vUV;
layout(location = 1) out vec3  vNormal;
layout(location = 2) out float vLight;
layout(location = 3) out float vViewZ;

void main() {
    vec4 clip = pc.mvp * vec4(inPos, 1.0);

    // Snap to low-res pixel grid in NDC, then re-multiply by w so the
    // rasterizer's perspective divide lands on the snapped position.
    vec2 grid    = pc.lowResSize.xy * 0.5;
    vec2 ndc     = clip.xy / clip.w;
    vec2 snapped = floor(ndc * grid + 0.5) / grid;
    clip.xy = snapped * clip.w;

    gl_Position = clip;
    vUV     = inUV;
    vNormal = inNormal;
    vLight  = inLight;
    // clip.w == -view-space Z for a standard perspective matrix; that's
    // the camera-forward distance, which is what we want for fog.
    vViewZ  = clip.w;
}
