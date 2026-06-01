#version 450

// Plastiboo fragment shader, used by both CubeRenderer and MapRenderer.
//
// Pipeline:
//   1. Cheap directional + ambient, banded into 4 woodcut cells.
//   2. Multiply by the per-vertex baked light (sector lighting).
//   3. Apply stylized banded fog (mix toward fog colour using a
//      quantized fog factor). The banding here is the engine signature -
//      smooth fog would just get crushed back into bands by the palette
//      pass with no control over where the breaks land; this way the
//      operator picks the band count.
//
// The post pass (dither + palette snap + warm grade) runs after this
// and finishes the look.

layout(location = 0) noperspective in vec2 vUV;
layout(location = 1) in vec3  vNormal;
layout(location = 2) in float vLight;
layout(location = 3) in float vViewZ;

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 lowResSize;
    vec4 fogColorEnable;   // rgb = colour, a = enable
    vec4 fogParams;        // x = start, y = end, z = bands
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // ---- Local shade ----
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(0.4, 0.8, 0.3));
    float ndotl = max(dot(N, L), 0.0);
    const float bands = 4.0;
    float lit = floor(ndotl * bands) / bands;
    float ambient = 0.18;
    float shade = clamp(lit + ambient, 0.0, 1.0);

    vec3 tex   = texture(uTex, vUV).rgb;
    vec3 color = tex * shade;

    // ---- Sector light multiplier ----
    color *= clamp(vLight, 0.0, 1.0);

    // ---- Stylized banded fog ----
    if (pc.fogColorEnable.a > 0.5) {
        float fogStart = pc.fogParams.x;
        float fogEnd   = pc.fogParams.y;
        float fogBands = max(pc.fogParams.z, 1.0);

        float t = clamp((vViewZ - fogStart) / max(fogEnd - fogStart, 1e-4),
                        0.0, 1.0);
        // Quantize fog factor into N steps so the fade matches the
        // palette's banded character. floor() rather than round() because
        // we want the *near* side of each band to remain at the previous
        // step - more readable distance cues.
        float fStep = floor(t * fogBands + 0.0) / fogBands;
        color = mix(color, pc.fogColorEnable.rgb, fStep);
    }

    outColor = vec4(color, 1.0);
}
