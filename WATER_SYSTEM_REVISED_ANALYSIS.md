# Water System - Revised Analysis
**Date:** 2025-11-24

## Correction to Initial Audit

I initially missed that you've **already implemented** the Ocean/Lake switching architecture in commit `87037fd215` ("lakes and stuff"). The code already has:

✅ **Ocean class** (FFT-based)
✅ **Lake class** (traditional)
✅ **Interior/Exterior detection**
✅ **Height-based switching**: `isOcean = !mInterior && (|mTop| <= 10.0f)`

## The REAL Problems

### Problem 1: Sea-Level Lakes Covered by Ocean

**Code:** [water.cpp:822](d:\Gamedev\openmw-snow\apps\openmw\mwrender\water.cpp#L822)
```cpp
bool isOcean = !mInterior && (std::abs(mTop) <= 10.0f);
```

**Issue:**
In Morrowind, some lakes are at sea level (height = 0.0). When you're in a cell with a sea-level lake, this triggers `isOcean = true`, which enables the ocean's 12.8km clipmap, covering the entire visible area including the lake.

**Examples:**
- Vivec canals: height = 0.0 → triggers ocean
- Some coastal pools: height = -2.0 to 5.0 → triggers ocean
- Harbors: height = 0.0 → triggers ocean

**What should happen:**
- Ocean should only render in true ocean cells (perimeter cells, deep water)
- Lakes at sea level should use Lake renderer, even if at height 0.0

---

### Problem 2: Single Global Water Height (mTop)

**Code:** [water.cpp:821](d:\Gamedev\openmw-snow\apps\openmw\mwrender\water.cpp#L821)
```cpp
void WaterManager::setHeight(const float height) {
    mTop = height;  // Single global height
    // ...
}
```

**Issue:**
Only ONE water height can be active at a time. When you move between cells, `setHeight()` updates `mTop` globally. This means:

- ❌ Can't see ocean (height 0) AND mountain lake (height 512) simultaneously
- ❌ When switching cells, all water jumps to new height
- ❌ No support for multiple lakes at different altitudes in view

**What you need:**
```cpp
std::vector<WaterPlane> mActiveWaterBodies;  // Multiple heights simultaneously
```

---

### Problem 3: No Spatial Bounds

**Ocean geometry:**
- Clipmap extends 12.8km radius from camera
- Follows camera (camera-centric positioning)
- No bounds checking → renders everywhere

**Lake geometry:**
[lake.cpp:82-96](d:\Gamedev\openmw-snow\apps\openmw\mwrender\lake.cpp#L82-L96)
```cpp
// Creates 100,000 × 100,000 unit quad
vertices->push_back(osg::Vec3f(-50000, -50000, 0));
vertices->push_back(osg::Vec3f(50000, -50000, 0));
// ...
```

**Issue:**
- Both ocean and lake render globally, no per-cell extent
- No way to constrain lake to specific area
- Ocean doesn't know to stop rendering near lakes

---

## What You Actually Need

### 1. Better Ocean Detection

Replace the simple ±10 threshold with smarter logic:

**Option A: Connectivity Check (Best)**
```cpp
bool isOcean(const CellStore* cell) {
    if (cell->isInterior()) return false;

    float height = cell->getWaterHeight();
    if (std::abs(height) > 10.0f) return false;  // Too high/low for ocean

    // Check if connected to ocean perimeter
    return isConnectedToPerimeter(cell);
}

bool isConnectedToPerimeter(const CellStore* cell) {
    // BFS: Does this water body reach world edge?
    // Cells at |gridX| > 25 or |gridY| > 25 are ocean
    // (cached at startup)
}
```

**Option B: Manual Override List (Simple)**
```cpp
// In config file or hardcoded
static const std::set<ESM::RefId> LAKE_CELLS = {
    "Vivec, Temple Canton",
    "Vivec, Foreign Quarter",
    "Balmora, Odai River",
    // ... etc
};

bool isOcean(const CellStore* cell) {
    if (LAKE_CELLS.count(cell->getId())) return false;  // Force lake
    return !cell->isInterior() && std::abs(cell->getWaterHeight()) <= 10.0f;
}
```

---

### 2. Multi-Height Water Support

**Goal:** Render ocean at 0.0 AND lake at 512.0 simultaneously

**Solution:** Track multiple active water bodies

```cpp
struct ActiveWaterBody {
    enum Type { Ocean, Lake } type;
    float height;
    osg::BoundingBox extent;  // Spatial bounds
    std::unique_ptr<WaterBody> renderer;  // Ocean or Lake instance
};

class WaterManager {
    std::vector<ActiveWaterBody> mActiveWaters;  // Instead of single mTop

    void updateActiveWaters(const osg::Vec3f& cameraPos) {
        mActiveWaters.clear();

        // Query all loaded cells
        for (const auto* cell : getLoadedCells()) {
            if (!cell->hasWater()) continue;

            ActiveWaterBody body;
            body.height = cell->getWaterHeight();
            body.extent = calculateExtent(cell);

            if (isOcean(cell)) {
                body.type = Ocean;
                body.renderer = mOcean;  // Reuse single ocean instance
            } else {
                body.type = Lake;
                body.renderer = std::make_unique<Lake>(...);  // One lake per water body
            }

            mActiveWaters.push_back(std::move(body));
        }
    }

    void update(float dt, bool paused, const osg::Vec3f& cameraPos) {
        for (auto& water : mActiveWaters) {
            if (isVisible(water.extent, cameraPos)) {
                water.renderer->setHeight(water.height);
                water.renderer->update(dt, paused, cameraPos);
            }
        }
    }
};
```

---

### 3. Add Spatial Extent per Water Body

**For Lakes:**
```cpp
osg::BoundingBox Lake::calculateExtent(const CellStore* cell) {
    float cellSize = 8192.0f;  // MW cell size
    int x = cell->getGridX();
    int y = cell->getGridY();
    float h = cell->getWaterHeight();

    // Lake bounded to cell
    return osg::BoundingBox(
        osg::Vec3f(x * cellSize, y * cellSize, h - 10),
        osg::Vec3f((x+1) * cellSize, (y+1) * cellSize, h + 100)
    );
}
```

**For Ocean:**
```cpp
bool Ocean::isVisible(const osg::Vec3f& cameraPos) const {
    // Don't render ocean high above sea level
    if (cameraPos.z() > mHeight + 500.0f) return false;

    // Don't render if in lake cell
    const CellStore* currentCell = getCurrentCell(cameraPos);
    if (currentCell && !isOcean(currentCell)) return false;

    return true;
}
```

---

## Implementation Priority

### Phase 1: Fix Ocean Covering Lakes (1-2 days)

**Quick Fix:**
1. Add manual lake cell list (hardcoded or config)
2. Modify `isOcean` check in [water.cpp:822](d:\Gamedev\openmw-snow\apps\openmw\mwrender\water.cpp#L822)
3. Test with Vivec, Balmora

```cpp
// water.cpp:822
bool isOceanCell(const MWWorld::CellStore* currentCell) {
    static const std::set<std::string> LAKE_CELLS = {
        "Vivec, Temple",
        "Vivec, Foreign",
        "Balmora, Odai River"
    };

    if (currentCell) {
        std::string cellName(currentCell->getCell()->getNameId());
        if (LAKE_CELLS.count(cellName)) return false;
    }

    return !mInterior && std::abs(mTop) <= 10.0f;
}
```

---

### Phase 2: Multi-Height Support (1 week)

**Goal:** See ocean + mountain lake simultaneously

1. Replace `float mTop` with `std::vector<ActiveWaterBody>`
2. Track water bodies per loaded cell
3. Update/render all visible water bodies
4. Update PhysicsSystem to check multiple water planes

---

### Phase 3: Spatial Extent (1 week)

**Goal:** Lakes don't render globally, only in their bounds

1. Add `extent` field to WaterBody
2. Implement per-cell bounds calculation
3. Visibility culling based on camera distance
4. Ocean altitude clipping (don't render on mountains)

---

## Key Insight

Your architecture is **already good** - you just need to:
1. **Refine the ocean detection** (avoid false positives for sea-level lakes)
2. **Support multiple concurrent water heights** (vector instead of single `mTop`)
3. **Add spatial bounds** (lakes don't render everywhere)

The full [WATER_SYSTEM_AUDIT_AND_IDEATION.md](d:\Gamedev\openmw-snow\WATER_SYSTEM_AUDIT_AND_IDEATION.md) document I wrote is still valuable for the detailed implementation, but I should have recognized you were further along than I initially thought!

---

## Immediate Next Step

**Recommended:** Start with Phase 1 (Manual Lake List)

Create this file:
```cpp
// apps/openmw/mwrender/watercellclassifier.hpp
#pragma once
#include <set>
#include <string>

namespace MWWorld { class CellStore; }

namespace MWRender {
    class WaterCellClassifier {
    public:
        static bool isOceanCell(const MWWorld::CellStore* cell);
    private:
        static const std::set<std::string> KNOWN_LAKE_CELLS;
    };
}
```

This will immediately fix the "ocean covering island" problem while you work on the full multi-height system.
