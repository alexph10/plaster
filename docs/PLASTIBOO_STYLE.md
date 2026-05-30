# The Plastiboo Look — Reference & Implementation Plan

This document captures the visual language we are targeting and translates each
element into a concrete rendering technique we can build in Vulkan.

## 1. Who is Plastiboo?

Plastiboo is an anonymous Italian illustrator best known for the *Vermis*
artbook series (Hollow Press, 2021–), pseudo-game guides for fantasy CRPGs that
do not exist. The work explicitly references:

- First-edition western RPG modules: *Wizardry*, *Eye of the Beholder*,
  *Bard's Tale*, early *Ultima* — first-person, dungeon-tile, fixed camera.
- Medieval woodcut, etching, and engraving (Albrecht Dürer, Hans Holbein).
- Weird-fantasy paperbacks and old occult/alchemical illustration.
- PS1- and DOS-era 3D, with its low resolution, dithering, and texture warping.

## 2. The visual vocabulary

| Element                  | What it looks like                                    | Where it comes from              |
|--------------------------|-------------------------------------------------------|----------------------------------|
| Restricted palette       | 2–8 colors, often near-monochrome with a sickly tint  | EGA / CGA / 1-bit displays       |
| Heavy ordered dithering  | Visible cross-hatch / 4×4 or 8×8 Bayer noise patterns | Halftone printing, EGA games     |
| Halftone dots            | CMYK-style circular dots that grow with darkness      | Newspaper print, comics          |
| Crushed contrast         | Deep blacks, grimy whites, almost no midtones         | High-contrast woodcuts           |
| Stretched photo textures | High-frequency rust/bone/stone projected onto shapes  | Plastiboo's own Photoshop method |
| Low effective resolution | The image clearly resolves to chunky pixels           | Old CRPG framebuffers (320×200)  |
| Vertex jitter            | Geometry edges visibly snap or shimmer in motion      | PS1 fixed-point vertex pipeline  |
| Affine UV warping        | Textures swim/skew along edges that face the camera   | PS1 lacking perspective-correct UVs |
| Paper / film grain       | Uniform high-frequency noise overlaid on the frame    | Print and analog film            |
| Vignette / scanlines     | Dark soft edges, optional CRT lines                   | CRT displays, old book scans     |

## 3. Rendering pipeline we are going to build

The whole thing is a three-stage pipeline. Everything that defines the look
happens between stages 1 and 2; the final blit just enlarges chunky pixels.

```diagram
                                                            ┌──────────────────┐
  scene geometry ──► [1] OFFSCREEN GEOMETRY PASS  ────────► │ color   320×240  │
                          render to low-res target          │ depth   320×240  │
                          (vertex snap, affine UV,          └────────┬─────────┘
                           grunge sample in world space)             │
                                                                     ▼
                                              [2] PALETTE + DITHER POST PASS
                                                  fullscreen triangle, reads:
                                                  • low-res color
                                                  • palette LUT (1D texture)
                                                  • Bayer / blue noise threshold
                                                  • optional halftone mask
                                                                     │
                                                                     ▼
                                              [3] NEAREST-FILTER BLIT TO SWAPCHAIN
                                                  vkCmdBlitImage, VK_FILTER_NEAREST
                                                  optional: vignette, grain, scanlines
                                                                     │
                                                                     ▼
                                                                  present
```

### 3.1 Stage 1 — offscreen geometry pass

Tiny color + depth attachments (start at 320×240, expose as a setting).

**Vertex snapping.** Before perspective divide, take the projected vertex and
round its `xy` to the nearest internal pixel grid. This is the PS1 jitter.

```glsl
// vertex shader (concept)
vec4 clip = uProj * uView * uModel * vec4(inPos, 1.0);
vec2 grid = vec2(uLowResWidth, uLowResHeight) * 0.5;
clip.xy = floor((clip.xy / clip.w) * grid + 0.5) / grid * clip.w;
gl_Position = clip;
```

**Affine UV (optional, per-material).** Disable perspective-correct
interpolation. In GLSL this is the `noperspective` qualifier on the UV
varyings. That gives PS1 texture-swim along skewed surfaces.

**World-space grunge sampling.** Sample a tiled "grunge" texture using world
position projected onto a chosen plane (triplanar is overkill; pick the
dominant normal axis). This makes scratches, hatching, and dirt feel painted
onto surfaces rather than UV-locked.

**Lighting.** Cheap directional + ambient. Quantize *the lighting term itself*
to N bands before multiplying by albedo so we get hard cell-shaded shadows
that match the woodcut feel.

### 3.2 Stage 2 — palette + dither post pass

A fullscreen triangle. The fragment shader does the entire Plastiboo
identity in three short steps.

**Step A — read & threshold.** Sample the low-res color. Look up a per-pixel
threshold from a small noise texture (see "Choice of dither" below):

```glsl
ivec2 px = ivec2(gl_FragCoord.xy);
float t = texelFetch(uThreshold, px % uThresholdSize, 0).r;  // in [0,1)
vec3 c = texture(uLowResColor, vUV).rgb;
c += (t - 0.5) * uDitherStrength;
```

**Step B — quantize to palette.** A 1D `RGBA8` LUT of N palette entries
(N typically 4–16). Find the nearest entry in linear RGB or in a perceptual
space (Oklab is overkill; Rec.709 luma works for monochrome palettes).

```glsl
vec3 quantize(vec3 col) {
    vec3 best = vec3(0);
    float bestD = 1e9;
    for (int i = 0; i < uPaletteSize; ++i) {
        vec3 p = texelFetch(uPalette, i, 0).rgb;
        float d = dot(col - p, col - p);
        if (d < bestD) { bestD = d; best = p; }
    }
    return best;
}
```

For larger palettes, replace the loop with a 3D LUT lookup (`sampler3D`,
typically 32³ or 64³) baked offline.

**Step C — halftone (optional).** Multiply or screen-blend a halftone dot
pattern keyed by luminance. The pattern is just a small repeating texture or
an analytic function:

```glsl
float halftone(vec2 uv, float intensity) {
    vec2 g = fract(uv * uHalftoneScale) - 0.5;
    return step(length(g), intensity);
}
```

### 3.3 Stage 3 — final blit

`vkCmdBlitImage` from the post-processed low-res image to the swapchain image
with `VK_FILTER_NEAREST`. This gives the chunky integer-scale upscale that
keeps the dither pattern crisp instead of bilinear-blurred.

Optional fullscreen overlays applied during the blit's draw (we'll use a draw,
not a raw blit, so we can layer extras):

- **Vignette:** `darken *= smoothstep(0.0, 1.0, length(uv - 0.5) * 1.5)`
- **Film grain:** sample a tiny tiling noise, multiply-blend at low strength
- **Scanlines (off by default):** `1.0 - 0.3 * step(0.5, fract(gl_FragCoord.y * 0.5))`

## 4. Choice of dither — Bayer vs. blue noise

We will support both, switchable at runtime.

| Property              | Bayer 8×8                          | Blue noise (128×128 tile)            |
|-----------------------|-------------------------------------|--------------------------------------|
| Pattern visibility    | Highly visible, geometric           | Hides as luminance variation         |
| Cost                  | Constant array, ~0                  | One texture fetch                    |
| Best for              | Print/halftone, EGA, Plastiboo look | Photoreal-lite, smoother gradients   |
| Temporal stability    | Locked to screen, no shimmer        | Locked to screen, no shimmer         |

Start with Bayer 8×8 because its visible patterning *is* the Plastiboo
signature. Keep blue noise as a toggle for screenshots where we want fewer
"computer pixels" and more "printed page."

## 5. Palettes

Palettes are runtime-swappable. Each is a 1D `RGBA8` texture loaded from a
small file (just a list of hex colors). Suggested starter set, all 4–6 entries
to start:

- **Bone & Pitch** — pure 2-color: ink black, parchment off-white. The Vermis
  default.
- **Plague** — sickly green/yellow on near-black. Three to four entries.
- **Cinder** — deep red and rust on cracked black. Three entries.
- **Bilious Forest** — cold green, ochre, charcoal. Four entries.
- **Reliquary** — desaturated gold and bone on black. Five entries.

Keep palette swaps cheap: a single `vkUpdateDescriptorSets` re-bind, no
pipeline change.

## 6. Mapping to engine modules

| Concept                          | Module / file                                  |
|----------------------------------|------------------------------------------------|
| Low-res offscreen target         | `Graphics/Renderer` (offscreen pass owner)     |
| Geometry pipeline (cube, mesh)   | `Graphics/Renderer` + `Graphics/Vulkan/Pipeline` |
| Post pipeline (palette + dither) | `Graphics/Post/` (new, future)                 |
| Palette LUT loading              | `Graphics/Post/Palette` (future)               |
| Threshold textures (Bayer/noise) | `Graphics/Post/Dither` (future)                |
| Grunge / overlay textures        | `Graphics/Textures/` (future)                  |

Right now (post-cleanup) we have `Graphics/Renderer`, `Graphics/Vulkan/`,
`Graphics/UI/`. The `Post/`, `Textures/`, and pipeline helpers land as we
implement roadmap steps 2–4.

## 7. Roadmap mapping (matches `README.md`)

1. **Vulkan bootstrap** — done.
2. **Textured cube via real graphics pipeline** — prerequisite for any of the
   stages above.
3. **Offscreen low-res target + nearest blit** — the moment the image becomes
   pixelated. Stage 1 and stage 3 of the pipeline.
4. **Palette quantize + Bayer dither post pass** — stage 2; this is the moment
   the engine starts looking like Plastiboo.
5. **Scene + camera** — needed to move around the cube and see vertex
   jitter / affine UV in action.
6. **Halftone, grunge overlay, grain, vignette, palette swaps** — polish layer
   on top of stage 2 and stage 3.

## 8. References

- *Vermis I & II* — Plastiboo / Hollow Press.
- "Deep dive into dithering" — gamelogic.co.za (Bayer vs. blue noise vs.
  error diffusion in shaders).
- Christoph Peters, "Free Blue Noise Textures" — moments-in-graphics.de.
- *Return of the Obra Dinn* (Lucas Pope) postmortem — TIGSource forum threads
  on 1-bit dither and per-object thresholds.
- PS1 GPU notes: no per-vertex perspective division for UVs, 16.16 fixed-point
  vertex positions, no z-buffer in many titles — the source of the jitter and
  affine warping we are deliberately reintroducing.
