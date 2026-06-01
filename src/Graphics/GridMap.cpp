#include "Graphics/GridMap.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace plaster {

GridMap::GridMap(int width, int height,
                 float cellSize, float floorY, float ceilingY)
    : m_width(width), m_height(height),
      m_cellSize(cellSize), m_floorY(floorY), m_ceilingY(ceilingY),
      m_cells(static_cast<size_t>(width) * height, Cell::Empty),
      m_floorYs(static_cast<size_t>(width) * height, floorY),
      m_ceilingYs(static_cast<size_t>(width) * height, ceilingY),
      m_lights(static_cast<size_t>(width) * height, 1.0f) {}

GridMap GridMap::FromAscii(const std::vector<std::string>& rows,
                           float cellSize, float floorY, float ceilingY) {
    if (rows.empty()) {
        throw std::runtime_error("GridMap::FromAscii: empty input");
    }
    const int height = static_cast<int>(rows.size());
    const int width  = static_cast<int>(rows[0].size());

    GridMap map(width, height, cellSize, floorY, ceilingY);
    for (int cy = 0; cy < height; ++cy) {
        if (static_cast<int>(rows[cy].size()) != width) {
            throw std::runtime_error("GridMap::FromAscii: rows must have equal length");
        }
        for (int cx = 0; cx < width; ++cx) {
            map.m_cells[cy * width + cx] =
                (rows[cy][cx] == '#') ? Cell::Wall : Cell::Empty;
        }
    }
    return map;
}

GridMap::Cell GridMap::At(int cx, int cy) const {
    if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) {
        // Out-of-bounds reads as solid wall so the mesh generator never
        // emits exterior faces that face into the void.
        return Cell::Wall;
    }
    return m_cells[cy * m_width + cx];
}

bool GridMap::IsWall(int cx, int cy)  const { return At(cx, cy) == Cell::Wall;  }
bool GridMap::IsEmpty(int cx, int cy) const { return At(cx, cy) == Cell::Empty; }

bool GridMap::IsWalkable(int cx, int cy) const {
    return cx >= 0 && cx < m_width && cy >= 0 && cy < m_height
        && m_cells[cy * m_width + cx] == Cell::Empty;
}

glm::vec3 GridMap::CellCenter(int cx, int cy) const {
    return glm::vec3((cx + 0.5f) * m_cellSize,
                     m_floorY,
                     (cy + 0.5f) * m_cellSize);
}

bool GridMap::IsBlockedAABB(float worldX, float worldZ, float radius) const {
    // Walk every cell overlapped by [x-r, x+r] x [z-r, z+r] in cell
    // coordinates. floor() handles negative coords correctly so we
    // properly catch the wall-of-bounds case (At() returns Wall for OOB).
    const int cx0 = static_cast<int>(std::floor((worldX - radius) / m_cellSize));
    const int cx1 = static_cast<int>(std::floor((worldX + radius) / m_cellSize));
    const int cy0 = static_cast<int>(std::floor((worldZ - radius) / m_cellSize));
    const int cy1 = static_cast<int>(std::floor((worldZ + radius) / m_cellSize));

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (IsWall(cx, cy)) return true;
        }
    }
    return false;
}

float GridMap::FloorYAt(int cx, int cy) const {
    if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) return m_floorY;
    if (m_cells[cy * m_width + cx] == Cell::Wall)            return m_floorY;
    return m_floorYs[cy * m_width + cx];
}

float GridMap::CeilingYAt(int cx, int cy) const {
    if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) return m_ceilingY;
    if (m_cells[cy * m_width + cx] == Cell::Wall)            return m_ceilingY;
    return m_ceilingYs[cy * m_width + cx];
}

void GridMap::SetFloorY(int cx, int cy, float y) {
    if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) return;
    m_floorYs[cy * m_width + cx] = y;
}

void GridMap::SetCeilingY(int cx, int cy, float y) {
    if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) return;
    m_ceilingYs[cy * m_width + cx] = y;
}

float GridMap::MinFloorY() const {
    float v = m_floorY;
    for (size_t i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i] == Cell::Empty) v = std::min(v, m_floorYs[i]);
    }
    return v;
}

float GridMap::MaxCeilingY() const {
    float v = m_ceilingY;
    for (size_t i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i] == Cell::Empty) v = std::max(v, m_ceilingYs[i]);
    }
    return v;
}

float GridMap::MaxFloorYInAABB(float worldX, float worldZ, float radius) const {
    const int cx0 = static_cast<int>(std::floor((worldX - radius) / m_cellSize));
    const int cx1 = static_cast<int>(std::floor((worldX + radius) / m_cellSize));
    const int cy0 = static_cast<int>(std::floor((worldZ - radius) / m_cellSize));
    const int cy1 = static_cast<int>(std::floor((worldZ + radius) / m_cellSize));

    bool any = false;
    float best = m_floorY;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) continue;
            if (m_cells[cy * m_width + cx] == Cell::Wall)            continue;
            const float f = m_floorYs[cy * m_width + cx];
            if (!any || f > best) { best = f; any = true; }
        }
    }
    return any ? best : m_floorY;
}

float GridMap::LightLevelAt(int cx, int cy) const {
    if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) return 1.0f;
    if (m_cells[cy * m_width + cx] == Cell::Wall)            return 1.0f;
    return m_lights[cy * m_width + cx];
}

void GridMap::SetLightLevel(int cx, int cy, float v) {
    if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) return;
    m_lights[cy * m_width + cx] = v;
}

float GridMap::MinLightInAABB(float worldX, float worldZ, float radius) const {
    const int cx0 = static_cast<int>(std::floor((worldX - radius) / m_cellSize));
    const int cx1 = static_cast<int>(std::floor((worldX + radius) / m_cellSize));
    const int cy0 = static_cast<int>(std::floor((worldZ - radius) / m_cellSize));
    const int cy1 = static_cast<int>(std::floor((worldZ + radius) / m_cellSize));

    bool any = false;
    float best = 1.0f;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) continue;
            if (m_cells[cy * m_width + cx] == Cell::Wall)            continue;
            const float l = m_lights[cy * m_width + cx];
            if (!any || l < best) { best = l; any = true; }
        }
    }
    return any ? best : 1.0f;
}

float GridMap::MinCeilingYInAABB(float worldX, float worldZ, float radius) const {
    const int cx0 = static_cast<int>(std::floor((worldX - radius) / m_cellSize));
    const int cx1 = static_cast<int>(std::floor((worldX + radius) / m_cellSize));
    const int cy0 = static_cast<int>(std::floor((worldZ - radius) / m_cellSize));
    const int cy1 = static_cast<int>(std::floor((worldZ + radius) / m_cellSize));

    bool any = false;
    float best = m_ceilingY;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height) continue;
            if (m_cells[cy * m_width + cx] == Cell::Wall)            continue;
            const float c = m_ceilingYs[cy * m_width + cx];
            if (!any || c < best) { best = c; any = true; }
        }
    }
    return any ? best : m_ceilingY;
}

void GridMap::FindFirstEmpty(int& outCx, int& outCy) const {
    for (int cy = 0; cy < m_height; ++cy) {
        for (int cx = 0; cx < m_width; ++cx) {
            if (m_cells[cy * m_width + cx] == Cell::Empty) {
                outCx = cx; outCy = cy;
                return;
            }
        }
    }
    outCx = -1; outCy = -1;
}

} // namespace plaster
