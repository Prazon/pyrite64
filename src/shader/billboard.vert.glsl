#version 460

// Per-vertex: same data for all 4 verts of a quad.
// gl_VertexIndex % 4 picks the corner and the UV.
layout (location = 0) in vec3 inPosition;     // billboard world center
layout (location = 1) in uint inObjectId;
layout (location = 2) in vec4 inColor;        // tint (unused for now)

layout (location = 0) out vec2 v_uv;
layout (location = 1) out flat uint v_objectID;
layout (location = 2) out vec4 v_color;

layout(std140, set = 1, binding = 0) uniform UniformGlobal {
    mat4 projMat;
    mat4 cameraMat;
    vec2 screenSize;
    vec2 spriteSize; // unused
};

// Per-billboard uniform pushed by the editor before each draw call.
// Holds the *cell* size and pivot in source-pixel units, the per-component
// pixelScale, and the UV sub-rect within the bound texture for the current
// sprite-sheet cell.
layout(std140, set = 1, binding = 1) uniform BillboardParams {
    // .xy = cell pixel size, .zw = pivot in cell-local pixels (0,0 = TL)
    vec4 sizeAndPivot;
    // .xy = uv0 (TL), .zw = uv1 (BR)
    vec4 uvRect;
    // .x = world-units-per-pixel (so a cellW*pixelScale "screen" sprite ends up
    // sized correctly in world space). 0 selects the legacy auto path.
    // .y = isSelected (0/1) — not used by shader yet, reserved.
    // .z = unused, .w = unused.
    vec4 mode;
};

void main()
{
    int corner = gl_VertexIndex % 4;

    // --- World-fixed axes ---
    // The quad lies in the world X-Y plane, facing +Z. Looks correct from the
    // gameplay camera (which looks down -Z at the X-Y plane). Orbiting the
    // editor camera shows the sprite edge-on at 90° — same as the runtime.
    vec3 worldRight = vec3(1.0, 0.0, 0.0);
    vec3 worldUp    = vec3(0.0, 1.0, 0.0);

    vec2 cellSize = sizeAndPivot.xy;
    vec2 pivot    = sizeAndPivot.zw;
    float worldPerPx = mode.x;

    // World-space half-extents of the quad. Pivot is the in-cell pixel that
    // should land at inPosition; we shift the quad so that pivot maps there.
    vec2 sizeW   = cellSize * worldPerPx;
    vec2 pivotW  = pivot    * worldPerPx;
    // Default pivot (0, cellH) = TL feet would put origin at top-left;
    // we follow the runtime convention "pivot is the in-cell pixel that lands
    // on inPosition". So the corner offset = corner_in_cell_px - pivot_px.
    // Corner-in-cell pixel positions: TL(0,0), TR(cellW,0), BR(cellW,cellH), BL(0,cellH).
    vec2 cornerPx;
    vec2 uv;
    if      (corner == 0) { cornerPx = vec2(0.0,         0.0);         uv = vec2(uvRect.x, uvRect.y); }
    else if (corner == 1) { cornerPx = vec2(cellSize.x,  0.0);         uv = vec2(uvRect.z, uvRect.y); }
    else if (corner == 2) { cornerPx = vec2(cellSize.x,  cellSize.y);  uv = vec2(uvRect.z, uvRect.w); }
    else                  { cornerPx = vec2(0.0,         cellSize.y);  uv = vec2(uvRect.x, uvRect.w); }

    vec2 offsetPx = cornerPx - pivot;        // pixels relative to anchor
    vec2 offsetW  = offsetPx * worldPerPx;   // world units

    // ImGui-style images grow downward in pixel space, but the world Y is
    // up — so flip the Y component when going to world.
    vec3 worldOffset = worldRight * offsetW.x + worldUp * (-offsetW.y);
    vec3 worldPos = inPosition + worldOffset;

    gl_Position = projMat * cameraMat * vec4(worldPos, 1.0);

    v_uv       = uv;
    v_objectID = inObjectId;
    v_color    = vec4(1.0, 1.0, 1.0, 1.0);
}
