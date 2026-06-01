#version 450

// Plastiboo post pass: ordered (Bayer 8x8) dither + nearest-palette
// quantization. Reads the low-res geometry color, writes the stylized
// low-res color.
//
// Pipeline within this shader:
//   1. Sample the geometry-pass color at this pixel.
//   2. Offset by a Bayer threshold so quantization error decorrelates per
//      pixel - this is the visible cross-hatch signature of the look.
//   3. Snap to the nearest palette entry.
//
// The Bayer matrix is a compile-time constant: it's 64 bytes of data, not
// worth the bookkeeping cost of a sampled image + descriptor binding.

layout(location = 0) in  vec2 vUV;   // unused; we sample by gl_FragCoord
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uColor;     // geometry pass output
layout(set = 0, binding = 1) uniform sampler1D uPalette;   // 1D LUT (max 16 entries)

layout(push_constant) uniform PushConstants {
    float ditherStrength;   // suggested 0.04 .. 0.12
    int   paletteSize;      // number of valid entries in uPalette
    int   enableDither;     // 0/1
    int   enablePalette;    // 0/1
    // ---- Colour grading (applied before dither + palette snap) ----
    //
    // The grading deliberately runs upstream of the palette snap so the
    // operator shapes which palette entry every pixel ends up matching,
    // rather than tinting already-quantized output (which would crush
    // the palette into bands). For a warm, high-saturation default look
    // we ship saturation > 1 and warmth > 0.
    float saturation;       // 1.0 = neutral; > 1.0 = punchier
    float warmth;           // 0.0 = neutral; > 0.0 = warmer (boost R/G, drop B)
} pc;

const uint kBayer8x8[64] = uint[64](
     0u, 32u,  8u, 40u,  2u, 34u, 10u, 42u,
    48u, 16u, 56u, 24u, 50u, 18u, 58u, 26u,
    12u, 44u,  4u, 36u, 14u, 46u,  6u, 38u,
    60u, 28u, 52u, 20u, 62u, 30u, 54u, 22u,
     3u, 35u, 11u, 43u,  1u, 33u,  9u, 41u,
    51u, 19u, 59u, 27u, 49u, 17u, 57u, 25u,
    15u, 47u,  7u, 39u, 13u, 45u,  5u, 37u,
    63u, 31u, 55u, 23u, 61u, 29u, 53u, 21u
);

float bayerThreshold(ivec2 px) {
    int idx = (px.x & 7) + (px.y & 7) * 8;
    // Values are 0..63; normalise to [0,1) and centre on 0 by subtracting 0.5.
    return float(kBayer8x8[idx]) * (1.0 / 64.0) - 0.5;
}

// Color grading: saturation pivots on Rec.601 luma so pure greys stay
// grey at any saturation value; warmth scales R/G up and B down so 0
// warmth is identity and positive warmth biases sunset / candlelight.
// Coefficients were picked so warmth in [-1, +1] gives a perceptually
// strong but not destructive shift; the engine ships with +0.2.
vec3 colourGrade(vec3 c) {
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, pc.saturation);

    float w = pc.warmth;
    c *= vec3(1.0 + 0.30 * w,
              1.0 + 0.05 * w,
              1.0 - 0.30 * w);

    return c;
}

vec3 nearestPaletteColor(vec3 c) {
    vec3  best     = vec3(0.0);
    float bestDist = 1e9;
    // paletteSize is bounded at 16 in the host code, so this loop is short.
    for (int i = 0; i < pc.paletteSize; ++i) {
        vec3 entry = texelFetch(uPalette, i, 0).rgb;
        vec3 d = c - entry;
        float dist = dot(d, d);
        if (dist < bestDist) {
            bestDist = dist;
            best = entry;
        }
    }
    return best;
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec3 c = texelFetch(uColor, px, 0).rgb;

    // 1) Grade (saturation, warmth) - shapes how pixels will be quantized.
    c = colourGrade(c);
    c = clamp(c, 0.0, 1.0);

    // 2) Dither - decorrelates banding the palette snap would otherwise show.
    if (pc.enableDither != 0) {
        float t = bayerThreshold(px);
        c += vec3(t * pc.ditherStrength);
    }

    // 3) Palette snap - the final character of the look.
    if (pc.enablePalette != 0) {
        c = nearestPaletteColor(c);
    }

    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
