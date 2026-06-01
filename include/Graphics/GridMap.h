#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace plaster {

// GridMap is a 2D tile grid representing Wolfenstein-style level geometry:
// every cell is either solid (a wall column) or empty (floor + ceiling).
//
// World mapping (right-handed, Y up):
//   cell (cx, cy) occupies world AABB  X in [cx, cx+1] * CellSize,
//                                       Z in [cy, cy+1] * CellSize,
//                                       Y in [FloorY, CeilingY].
//   cy = 0           -> "north" (smallest Z)
//   cy = Height-1    -> "south" (largest  Z)
//   cx = 0           -> "west"
//   cx = Width-1     -> "east"
//
// A default-yaw FPS camera (yaw=0, pitch=0) looks down -Z, i.e. north.
class GridMap {
public:
    enum class Cell : uint8_t { Empty = 0, Wall = 1 };

    GridMap() = default;
    GridMap(int width, int height,
            float cellSize, float floorY, float ceilingY);

    // Build a map from an ASCII layout. Each string is one map row.
    // '#' = wall, anything else = empty. All rows must be the same length.
    static GridMap FromAscii(const std::vector<std::string>& rows,
                             float cellSize = 1.0f,
                             float floorY   = 0.0f,
                             float ceilingY = 2.5f);

    int   Width()      const { return m_width;  }
    int   Height()     const { return m_height; }
    float CellSize()   const { return m_cellSize; }

    // Map-wide default floor/ceiling. Per-cell heights default to these.
    // Also serve as fallback Y bounds for the player when no GridMap
    // lookup makes sense (e.g. outside the grid).
    float FloorY()     const { return m_floorY;   }
    float CeilingY()   const { return m_ceilingY; }

    Cell  At(int cx, int cy) const;
    bool  IsWall(int cx, int cy) const;
    bool  IsEmpty(int cx, int cy) const;
    // Returns true only for in-bounds empty cells.
    bool  IsWalkable(int cx, int cy) const;

    // Per-cell heights. For walls and out-of-bounds cells, FloorYAt
    // returns the map's default FloorY and CeilingYAt the default
    // CeilingY - the mesh generator only consults these for empty cells.
    float FloorYAt(int cx, int cy)   const;
    float CeilingYAt(int cx, int cy) const;

    // Set per-cell floor / ceiling. Out-of-bounds calls are silently
    // ignored. Caller is expected to set these on empty cells only;
    // calling on a wall cell does nothing useful (walls aren't drawn).
    void  SetFloorY(int cx, int cy, float y);
    void  SetCeilingY(int cx, int cy, float y);

    // Min FloorY / Max CeilingY across the whole map. Used for the
    // engine's vertical bound when clamping arbitrary camera positions.
    float MinFloorY()   const;
    float MaxCeilingY() const;

    // Highest FloorY among empty cells overlapped by an XZ-AABB centred
    // at (worldX, worldZ) with half-extent `radius`. Used by the player
    // controller to stand on the tallest step under the player's feet
    // (classic step-climbing). Falls back to the map default if no empty
    // cell is overlapped (player is somehow outside; shouldn't happen if
    // IsBlockedAABB is honoured).
    float MaxFloorYInAABB(float worldX, float worldZ, float radius) const;

    // Lowest CeilingY among empty cells overlapped by the same AABB,
    // used as the head-clearance upper bound.
    float MinCeilingYInAABB(float worldX, float worldZ, float radius) const;

    // Per-cell light level in [0, 1]. Multiplies into shaded colour at
    // fragment time, so 1.0 = fully lit (default), 0.0 = pitch black.
    // Walls inherit the *empty neighbour's* light by construction (the
    // mesh generator emits each wall slice into one empty cell).
    // Out-of-bounds and wall cells return 1.0 - the mesh generator only
    // queries this for empty cells anyway.
    float LightLevelAt(int cx, int cy) const;
    void  SetLightLevel(int cx, int cy, float v);

    // Lowest light level across empty cells overlapped by an XZ-AABB,
    // used to assign a single light value to a sprite that straddles
    // a cell boundary (the sprite picks up the darker neighbour, which
    // reads as "in shadow").
    float MinLightInAABB(float worldX, float worldZ, float radius) const;

    // World-space centre of a cell (Y at floor level).
    glm::vec3 CellCenter(int cx, int cy) const;

    // Find the first empty cell scanning row-major. Returns (-1, -1) when
    // the map has no empty cells.
    void FindFirstEmpty(int& outCx, int& outCy) const;

    // Returns true if an axis-aligned bounding box centred at (worldX,
    // worldZ) on the XZ plane with half-extent `radius` overlaps any wall
    // cell (including the out-of-bounds "solid" border). Used by the
    // player controller to do axis-separated slide collision: try the X
    // move alone, then the Z move alone, accepting whichever components
    // don't enter a wall.
    bool IsBlockedAABB(float worldX, float worldZ, float radius) const;

private:
    int   m_width  = 0;
    int   m_height = 0;
    float m_cellSize = 1.0f;
    float m_floorY   = 0.0f;
    float m_ceilingY = 2.5f;
    std::vector<Cell>  m_cells;    // row-major: index = cy * width + cx
    std::vector<float> m_floorYs;  // per-cell floor; init = m_floorY
    std::vector<float> m_ceilingYs;// per-cell ceiling; init = m_ceilingY
    std::vector<float> m_lights;   // per-cell light in [0,1]; init = 1.0
};

} // namespace plaster
