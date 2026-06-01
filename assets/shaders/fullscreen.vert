#version 450

// Fullscreen-triangle vertex shader. No vertex buffer required: the three
// gl_VertexIndex values 0..2 are mapped to clip-space positions that form a
// single triangle large enough to cover the entire viewport.
//
// Why one triangle instead of a quad? The diagonal seam between the two
// triangles of a quad is a real bandwidth/cache hazard on tiled GPUs; a
// single oversized triangle has no seam and rasterises identically across
// the visible region.
//
// gl_VertexIndex -> (x, y) in clip space:
//   0 -> (-1, -1)
//   1 -> ( 3, -1)
//   2 -> (-1,  3)
//
// UV is derived so that the visible [0,1]^2 region maps to the screen.

layout(location = 0) out vec2 vUV;

void main() {
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2),
                    float( gl_VertexIndex       & 2));
    // pos is now (0,0), (2,0), (0,2). Map to clip space (-1..3 range):
    vUV = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
