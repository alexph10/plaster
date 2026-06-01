#version 450

// Sprite fragment: hard alpha-tested cutout, with per-sprite sector
// light and the same banded fog the world geometry uses. Sprites stay
// "in the room they're standing in" because vLight comes from the cell
// at the sprite's feet (assigned host-side by Application).

layout(location = 0) in vec2  vUV;
layout(location = 1) in float vLight;
layout(location = 2) in float vViewZ;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 cameraPosLight;
    vec4 worldCenterFogStart;
    vec4 sizeFog;
    vec4 fogColorEnable;
} pc;

void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < 0.5) discard;

    vec3 color = c.rgb * clamp(vLight, 0.0, 1.0);

    if (pc.fogColorEnable.a > 0.5) {
        float fogStart = pc.worldCenterFogStart.w;
        float fogEnd   = pc.sizeFog.z;
        float fogBands = max(pc.sizeFog.w, 1.0);

        float t = clamp((vViewZ - fogStart) / max(fogEnd - fogStart, 1e-4),
                        0.0, 1.0);
        float fStep = floor(t * fogBands) / fogBands;
        color = mix(color, pc.fogColorEnable.rgb, fStep);
    }

    outColor = vec4(color, 1.0);
}
