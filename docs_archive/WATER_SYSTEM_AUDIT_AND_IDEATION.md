# OpenMW FFT Ocean - Water System Audit & Multi-Altitude Water Ideation

**Date:** 2025-11-24
**Base Commit:** bb3b3eb5e498183ae8c804810d6ebdba933dbeb2
**Current Branch:** water

---

## EXECUTIVE SUMMARY

**Current State:** The FFT ocean implementation is ~90% complete but has a critical architectural limitation preventing proper support for multiple water types (ocean, lakes, rivers) at different altitudes.

**Root Problem:** The water management system uses a **single global water height** and simple boolean logic to choose between Ocean (FFT) and Lake (traditional), causing the ocean to cover entire islands when active.

**Solution Required:** Implement **per-cell water body detection** with altitude-based filtering to support multiple independent water planes coexisting in the scene.

---

## PART 1: ARCHITECTURAL AUDIT

### 1.1 Current Water System Architecture

#### Class Hierarchy

```
WaterManager (Singleton, apps/openmw/mwrender/water.hpp:57-158)
├── Traditional Water Rendering (Legacy System)
│   ├── mWaterNode (PositionAttitudeTransform) - The original flat water plane
│   ├── mWaterGeom (Geometry) - Simple quad mesh
│   ├── mRefraction (Refraction Camera - RTT)
│   ├── mReflection (Reflection Camera - RTT)
│   └── mSimulation (Ripple simulation - RTT)
│
├── Ocean (FFT-based, NEW)
│   └── mOcean (unique_ptr<Ocean>) - Inherits from WaterBody
│
└── Lake (Traditional water, NEW)
    └── mLake (unique_ptr<Lake>) - Inherits from WaterBody
```

#### WaterBody Interface

**File:** [waterbody.hpp](d:\Gamedev\openmw-snow\apps\openmw\mwrender\waterbody.hpp#L15-L28)

```cpp
class WaterBody {
    virtual void setEnabled(bool enabled) = 0;
    virtual void update(float dt, bool paused, const osg::Vec3f& cameraPos) = 0;
    virtual void setHeight(float height) = 0;
    virtual bool isUnderwater(const osg::Vec3f& pos) const = 0;
    virtual void addToScene(osg::Group* parent) = 0;
    virtual void removeFromScene(osg::Group* parent) = 0;
};
```

**Design Assessment:** ✅ **Good interface** - pluggable, clean separation of concerns

---

### 1.2 Water Height Flow (How Water Gets Positioned)

#### Data Flow Chain

```
1. ESM/ESM4 Cell Data (Game Files)
   ├── Cell::mWaterHeight (float)
   ├── Cell::mHasWater (bool)
   └── Cell::isExterior (bool)
        ↓
2. MWWorld::Scene::loadCell() (scene.cpp:506-511)
   ├── Reads: cellVariant.hasWater()
   ├── Reads: cell.getWaterLevel()
   └── Calls: mRendering.setWaterHeight(waterLevel)
        ↓
3. RenderingManager::setWaterHeight() (renderingmanager.cpp:1007-1014)
   ├── Calls: mWater->setHeight(height)
   ├── Calls: mSky->setWaterHeight(height)
   └── Calls: mPhysics->enableWater(waterLevel)
        ↓
4. WaterManager::setHeight() (water.cpp:819-848)
   ├── Sets: mTop = height (GLOBAL WATER HEIGHT)
   ├── Determines: isOcean = !mInterior && (|mTop| <= 10.0f)
   ├── Calls: mOcean->setHeight(height)
   ├── Calls: mLake->setHeight(height)
   └── Updates RTT camera planes
```

**Critical Issue Identified:**

```cpp
// water.cpp:819-834
void WaterManager::setHeight(const float height) {
    mTop = height;  // ❌ SINGLE GLOBAL HEIGHT - This is the problem!

    bool isOcean = !mInterior && (std::abs(mTop) <= 10.0f);
    // ❌ All water at sea level (±10 units) = Ocean
    // ❌ All water above/below 10 units = Lake
    // ❌ No per-cell discrimination

    if (mUseOcean && mOcean) {
        mOcean->setHeight(height);
        mOcean->setEnabled(mEnabled && isOcean);
    }
    if (mLake) {
        mLake->setHeight(height);
        mLake->setEnabled(mEnabled && !isOcean);
    }
}
```

---

### 1.3 Why Ocean Covers the Entire Island

#### Problem Root Cause Analysis

**The ±10 Unit Threshold Logic:**

In Morrowind, the standard ocean sea level is **0.0 units**. The current code treats ANY water height within ±10 units of zero as "ocean":

```cpp
bool isOcean = !mInterior && (std::abs(mTop) <= 10.0f);
```

**Morrowind Island Geography:**
- **Ocean surrounding island:** Water height = 0.0 (sea level)
- **Coastal waters:** Water height ≈ 0.0 to -5.0
- **Harbors/ports:** Water height ≈ 0.0 to 5.0
- **Some lakes:** Water height = 0.0 (same as sea level!)
- **High-altitude lakes:** Water height > 10.0 or < -10.0

**Consequence:**
When the player enters ANY cell with water at sea level (including some lakes), the entire Ocean system activates. The Ocean's clipmap geometry extends to the **horizon** (~12.8km radius), covering the entire visible island.

**Example Scenario:**
```
Player in Vivec City:
  └─> Cell water height: 0.0 (sea level)
      └─> isOcean = true (matches threshold)
          └─> Ocean enabled globally
              └─> Ocean clipmap covers 25.6km diameter
                  └─> Ocean renders over ALL water, including:
                      - Vivec canals (should be separate water)
                      - Nearby lakes (should be separate water)
                      - Distant coastal lakes (should be separate water)
```

#### Current Visibility Logic

**File:** [water.cpp:891-906](d:\Gamedev\openmw-snow\apps\openmw\mwrender\water.cpp#L891-L906)

```cpp
void WaterManager::updateVisible() {
    bool visible = mEnabled && mToggled;

    bool isOcean = !mInterior && (std::abs(mTop) <= 10.0f);
    bool useNewWater = (mUseOcean && isOcean) || (mLake && !isOcean);

    // Hide traditional water when Ocean/Lake active
    mWaterNode->setNodeMask((visible && !useNewWater) ? ~0u : 0u);

    // RTT cameras always active when water visible
    if (mRefraction) mRefraction->setNodeMask(visible ? Mask_RenderToTexture : 0u);
    if (mReflection) mReflection->setNodeMask(visible ? Mask_RenderToTexture : 0u);
}
```

**Issue:** Traditional water plane is hidden globally when Ocean is active, even though it should still render for lakes at different altitudes.

---

### 1.4 Ocean Implementation Details

#### Ocean Geometry (Clipmap LOD System)

**File:** [ocean.cpp:814-872](d:\Gamedev\openmw-snow\apps\openmw\mwrender\ocean.cpp#L814-L872)

**Clipmap Ring Structure:**
```
Ring 0 (Center):   512×512 grid,  radius 1,813 MW units    (2.5m per vertex)
Ring 1:            128×128 grid,  radius 3,626 MW units    (10m per vertex)
Ring 2:             64×64 grid,   radius 7,253 MW units    (40m per vertex)
Ring 3:             32×32 grid,   radius 14,506 MW units   (160m per vertex)
Rings 4-8:          Repeated rings extending to 232,096 MW units (~3.2km radius)
```

**Coverage:** The ocean geometry extends approximately **12.8km in all directions** from the camera (when fully extended with horizon rings).

**Positioning:**
```cpp
// ocean.cpp:173-202
void Ocean::update(float dt, bool paused, const osg::Vec3f& cameraPos) {
    // Mesh stays at origin, shader offsets based on camera position
    mRootNode->setPosition(vec3(0, 0, mHeight));
    mCameraPositionUniform->set(cameraPos);
}
```

**Critical Insight:** The ocean mesh is **camera-centric** - it follows the player and renders in a huge radius. This is correct for an infinite ocean, but incorrect when there should be distinct water bodies (lakes, rivers, ocean) at different locations and altitudes.

#### FFT Cascade System

**File:** [ocean.cpp:440-516](d:\Gamedev\openmw-snow\apps\openmw\mwrender\ocean.cpp#L440-L516)

**4 Wave Cascades:**
- **Cascade 0:** 50m tile (3,626 MW units) - Fine ripples
- **Cascade 1:** 100m tile (7,253 MW units) - Medium waves
- **Cascade 2:** 200m tile (14,506 MW units) - Large swells
- **Cascade 3:** 400m tile (29,012 MW units) - Ocean waves

Each cascade computes displacement and normals via FFT, creating realistic ocean waves with proper physics simulation (JONSWAP spectrum, dispersion relation, foam generation).

**Assessment:** ✅ **Excellent implementation** - FFT system is sound, just needs proper spatial constraints.

---

### 1.5 Lake Implementation Status

**File:** [lake.cpp:1-125](d:\Gamedev\openmw-snow\apps\openmw\mwrender\lake.cpp)

**Current State:** ⚠️ **Stub Implementation**

```cpp
// Lake geometry: Simple 100k×100k unit quad
// Shader: Solid blue color (0.0, 0.3, 0.8)
```

**Lake Shaders:**
- [lake.vert](d:\Gamedev\openmw-snow\files\shaders\compatibility\lake.vert) - 9 lines, basic passthrough
- [lake.frag](d:\Gamedev\openmw-snow\files\shaders\compatibility\lake.frag) - 9 lines, solid color

**Missing Features:**
- ❌ No normal mapping
- ❌ No reflection/refraction sampling
- ❌ No foam
- ❌ No animated waves (could use simplified FFT or procedural)
- ❌ No proper lighting integration

**Positioning:**
```cpp
// lake.cpp:73-102
Lake::Lake(...) {
    // Creates huge 100,000 unit quad at height 0
    // Positioned via setHeight() later
}
```

---

### 1.6 Multiple Water Planes - Current Limitations

#### What Works
- ✅ Ocean and Lake can be toggled independently
- ✅ WaterBody interface supports polymorphism
- ✅ Each water type can have independent height
- ✅ Reflection/refraction RTT works for current water

#### What Doesn't Work
- ❌ Only ONE water body active at a time (ocean XOR lake)
- ❌ Only ONE global water height (`mTop`)
- ❌ No per-cell water body tracking
- ❌ No spatial extent/bounds for water bodies
- ❌ Ocean covers entire visible area (camera-centric)
- ❌ Lake also covers entire visible area (huge quad)
- ❌ No altitude-based water body selection
- ❌ Traditional water plane hidden when new systems active

#### Architecture Gap

**Missing Component:** Water body registry with spatial extent

```
Current:
  WaterManager
  ├── mTop (single global height)
  ├── mOcean (active/inactive)
  └── mLake (active/inactive)

Needed:
  WaterManager
  ├── mActiveBodies (vector<WaterBodyInstance>)
  │   ├── Body 0: { type=Ocean, height=0.0, extent=Infinite }
  │   ├── Body 1: { type=Lake, height=512.0, extent=BoundingBox(x,y,z) }
  │   └── Body 2: { type=Lake, height=128.0, extent=BoundingBox(x,y,z) }
  └── mCellWaterMap (map<CellID, WaterBodyInstance*>)
```

---

## PART 2: PROBLEM STATEMENT

### 2.1 User Requirements

**Goal:** Support three types of water in OpenMW:

1. **Ocean** (FFT-based)
   - Infinite extent, follows camera
   - Sea level (height ≈ 0.0)
   - Realistic FFT waves, foam, PBR lighting
   - Should cover entire ocean, but ONLY ocean

2. **Lakes** (Traditional or simplified FFT)
   - Bounded extent (defined by cell data or detection algorithm)
   - Variable altitude (can be at any Z height)
   - Multiple lakes can exist at different altitudes
   - Should NOT be covered by ocean
   - Must trigger swimming animation

3. **Rivers** (Future)
   - Flowing water with directional current
   - Variable altitude (follows terrain)
   - Bounded extent along riverbed

### 2.2 Current Failures

**Issue #1: Ocean Covering Entire Island**
- **Symptom:** Ocean renders everywhere, covering lakes and land
- **Root Cause:** Clipmap geometry extends 12.8km radius from camera
- **Trigger:** Any cell with water height ±10 units activates global ocean

**Issue #2: Lakes Not Visible or Covered**
- **Symptom:** Lakes either missing or buried under ocean
- **Root Cause:** Lake disabled when ocean active (mutual exclusion)
- **Additional:** Lake implementation incomplete (solid blue)

**Issue #3: No Multi-Altitude Support**
- **Symptom:** Only one water height at a time
- **Root Cause:** Single `mTop` variable in WaterManager
- **Impact:** Can't have lake at 512 units + ocean at 0 units simultaneously

### 2.3 Desired Behavior

**Scenario: Player on Coastal Island with Mountain Lake**

```
Camera Position: (10000, 8000, 256)  // On hillside

Visible Water Bodies:
1. Ocean
   - Height: 0.0
   - Extent: Infinite (but only render below elevation 50)
   - Distance: 2km away (visible at horizon)
   - Rendering: FFT ocean shader, clipmap LOD

2. Mountain Lake A
   - Height: 512.0
   - Extent: BoundingBox(9500, 7500, 512) to (10500, 8500, 512)
   - Distance: 500m away (clearly visible)
   - Rendering: Lake shader (simplified waves)

3. Mountain Lake B
   - Height: 768.0
   - Extent: BoundingBox(11000, 9000, 768) to (11500, 9500, 768)
   - Distance: 1.5km away (visible in distance)
   - Rendering: Lake shader

Expected Result:
- Ocean renders at horizon, below mountain
- Both lakes render at their respective altitudes
- No water bodies overlap or hide each other
- All three trigger swimming when player enters
```

**Current Result:**
- Ocean active (covers entire scene)
- Lakes hidden (disabled when ocean active)
- Can't see distinct water bodies

---

## PART 3: IDEATION - SOLUTIONS & APPROACHES

### 3.1 Solution Architecture Overview

**Core Concept:** Transition from **global binary switch** (ocean XOR lake) to **multi-body spatial registry** with altitude-based filtering.

#### Proposed Architecture

```cpp
// New Classes

struct WaterBodyDescriptor {
    enum Type { Ocean, Lake, River };

    Type type;
    float height;              // Z altitude
    osg::BoundingBox extent;   // Spatial bounds (infinite for ocean)
    bool infinite;             // True for ocean (follows camera)
    ESM::RefId cellId;         // Originating cell (for lakes/rivers)
};

class WaterBodyInstance {
    WaterBodyDescriptor descriptor;
    std::unique_ptr<WaterBody> implementation;  // Ocean or Lake
    bool visible;              // Visibility culling result
};

class WaterBodyRegistry {
    std::vector<WaterBodyInstance> mBodies;
    std::map<ESM::RefId, WaterBodyDescriptor*> mCellWaterMap;

    void registerWaterBody(const WaterBodyDescriptor& desc);
    void unregisterWaterBody(const ESM::RefId& cellId);
    std::vector<WaterBodyInstance*> getVisibleBodies(const osg::Vec3f& cameraPos, float maxDist);
};

// Modified WaterManager

class WaterManager {
    WaterBodyRegistry mRegistry;     // NEW: Multi-body management

    // Removed: mTop (single height)
    // Removed: isOcean boolean logic

    void updateVisibleWaterBodies(const osg::Vec3f& cameraPos);
    void detectWaterType(const MWWorld::CellStore* cell);
};
```

---

### 3.2 Water Type Detection Algorithm

**Goal:** Automatically detect whether a cell's water should be Ocean, Lake, or River.

#### Detection Strategy

##### Option A: Heuristic-Based (Recommended)

**Algorithm:**
```cpp
WaterBodyDescriptor::Type detectWaterType(const MWWorld::CellStore* cell) {
    if (!cell->getCell()->hasWater())
        return None;

    float waterHeight = cell->getCell()->getWaterHeight();
    bool isExterior = cell->getCell()->isExterior();

    // Interior cells = always lakes
    if (!isExterior)
        return Lake;

    // Exterior cell water classification
    const float SEA_LEVEL = 0.0f;
    const float SEA_LEVEL_TOLERANCE = 5.0f;  // ±5 units (not ±10)

    // 1. Ocean: Exterior + at sea level
    if (std::abs(waterHeight - SEA_LEVEL) <= SEA_LEVEL_TOLERANCE) {
        // Additional check: Is this connected to main ocean?
        if (isConnectedToOcean(cell))
            return Ocean;
        else
            return Lake;  // Coastal pond/harbor at sea level but isolated
    }

    // 2. Lake: Exterior + elevated
    if (waterHeight > SEA_LEVEL + SEA_LEVEL_TOLERANCE)
        return Lake;

    // 3. Lake: Exterior + below sea level (underwater caves, etc.)
    if (waterHeight < SEA_LEVEL - SEA_LEVEL_TOLERANCE)
        return Lake;

    // Default: Lake
    return Lake;
}

bool isConnectedToOcean(const MWWorld::CellStore* cell) {
    // Check if cell is on world perimeter (ocean boundary)
    int gridX = cell->getCell()->getGridX();
    int gridY = cell->getCell()->getGridY();

    // Morrowind world: roughly -30 to +30 grid range
    // Ocean cells are typically at perimeter
    const int PERIMETER_THRESHOLD = 25;

    if (std::abs(gridX) > PERIMETER_THRESHOLD || std::abs(gridY) > PERIMETER_THRESHOLD)
        return true;  // Likely ocean

    // Alternative: Check neighbor cells for continuous water at sea level
    // (More complex, requires world query)
    return false;
}
```

**Pros:**
- ✅ Simple, fast
- ✅ No manual tagging required
- ✅ Works with existing data
- ✅ Handles most common cases

**Cons:**
- ⚠️ May misclassify coastal ponds at sea level
- ⚠️ Requires tuning thresholds per game (Morrowind vs Skyrim)

---

##### Option B: Connectivity Analysis (Most Accurate)

**Algorithm:**
```cpp
enum WaterType detectWaterTypeByConnectivity(const MWWorld::CellStore* startCell) {
    // BFS to find connected water cells
    std::set<osg::Vec2i> visited;
    std::queue<osg::Vec2i> frontier;
    frontier.push(startCell->getGridCoords());

    float waterHeight = startCell->getCell()->getWaterHeight();
    bool reachedPerimeter = false;
    int connectedCellCount = 0;

    while (!frontier.empty()) {
        osg::Vec2i pos = frontier.front();
        frontier.pop();

        if (visited.count(pos)) continue;
        visited.insert(pos);
        connectedCellCount++;

        // Check if at world perimeter
        if (isPerimeterCell(pos))
            reachedPerimeter = true;

        // Explore neighbors with same water height (±1 unit tolerance)
        for (auto neighbor : getNeighborCells(pos)) {
            const Cell* cell = getCell(neighbor);
            if (!cell || !cell->hasWater()) continue;

            if (std::abs(cell->getWaterHeight() - waterHeight) < 1.0f)
                frontier.push(neighbor);
        }
    }

    // Ocean: Reaches perimeter AND large connected area
    if (reachedPerimeter && connectedCellCount > 10)
        return Ocean;

    // Lake: Isolated or small connected area
    return Lake;
}
```

**Pros:**
- ✅ Most accurate (true ocean detection)
- ✅ Handles complex coastlines
- ✅ Detects isolated sea-level ponds correctly

**Cons:**
- ❌ Computationally expensive (BFS over cells)
- ❌ May need caching/precomputation
- ❌ Requires access to world cell data

---

##### Option C: Manual Tagging (Fallback)

**Data Structure:**
```cpp
// In settings or data file
struct WaterOverride {
    ESM::RefId cellId;
    WaterBodyDescriptor::Type forcedType;
};

std::map<ESM::RefId, WaterBodyDescriptor::Type> waterTypeOverrides = {
    { "Vivec, Temple Canton", Lake },      // Force specific cantons to lake
    { "Balmora, Odai River", River },      // Future: Mark rivers
    { "Bitter Coast Region", Ocean },      // Force coastal cells to ocean
};
```

**Pros:**
- ✅ 100% accurate (human-curated)
- ✅ Handles edge cases
- ✅ Can be modded

**Cons:**
- ❌ Requires manual work
- ❌ Doesn't scale to new mods/lands
- ❌ Maintenance burden

---

##### Recommended Hybrid Approach

**Strategy:**
1. Use **Heuristic-Based (Option A)** as default
2. Add **Manual Overrides (Option C)** for known edge cases
3. Optionally use **Connectivity Analysis (Option B)** for ocean cells only (cached at startup)

**Pseudocode:**
```cpp
WaterType detectWaterType(const CellStore* cell) {
    // 1. Check manual override
    if (hasOverride(cell->getId()))
        return getOverride(cell->getId());

    // 2. Apply heuristic
    WaterType heuristic = detectWaterTypeHeuristic(cell);

    // 3. If heuristic says Ocean, validate with connectivity (cached)
    if (heuristic == Ocean) {
        if (!oceanConnectivityCache.contains(cell->getGridCoords()))
            oceanConnectivityCache.compute();  // BFS once at startup

        if (!oceanConnectivityCache.isOcean(cell->getGridCoords()))
            return Lake;  // Override: Isolated sea-level water
    }

    return heuristic;
}
```

---

### 3.3 Water Body Extent Calculation

**Challenge:** Define spatial bounds for lakes to prevent infinite rendering.

#### Option A: Cell-Based Bounding Box

**Approach:**
```cpp
osg::BoundingBox calculateLakeExtent(const CellStore* cell) {
    // Use cell boundaries as water extent
    float cellSize = 8192.0f;  // Morrowind cell size in MW units
    int gridX = cell->getCell()->getGridX();
    int gridY = cell->getCell()->getGridY();

    float waterHeight = cell->getCell()->getWaterHeight();

    osg::Vec3f min(
        gridX * cellSize,
        gridY * cellSize,
        waterHeight - 10.0f  // Allow some depth
    );
    osg::Vec3f max(
        (gridX + 1) * cellSize,
        (gridY + 1) * cellSize,
        waterHeight + 100.0f  // Allow waves above surface
    );

    return osg::BoundingBox(min, max);
}
```

**Pros:**
- ✅ Simple, fast
- ✅ Aligns with OpenMW's cell streaming

**Cons:**
- ⚠️ Lakes spanning multiple cells need merging
- ⚠️ Sharp cutoffs at cell boundaries

---

#### Option B: Flood-Fill Connected Cells

**Approach:**
```cpp
osg::BoundingBox calculateLakeExtent(const CellStore* startCell) {
    std::set<osg::Vec2i> lakeCells = floodFillWaterCells(startCell);

    // Find min/max bounds of all connected cells
    osg::Vec3f min(FLT_MAX, FLT_MAX, FLT_MAX);
    osg::Vec3f max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (auto gridPos : lakeCells) {
        osg::BoundingBox cellBox = getCellBounds(gridPos);
        min.x() = std::min(min.x(), cellBox.xMin());
        min.y() = std::min(min.y(), cellBox.yMin());
        // ... (expand bounds)
    }

    return osg::BoundingBox(min, max);
}
```

**Pros:**
- ✅ Accurate (follows actual lake shape)
- ✅ Handles multi-cell lakes

**Cons:**
- ❌ Computationally expensive
- ❌ Needs caching

---

#### Option C: Ocean = Infinite Extent

**Approach:**
```cpp
struct WaterBodyDescriptor {
    bool infinite;  // True for ocean

    bool intersects(const osg::Vec3f& cameraPos, float maxDist) const {
        if (infinite) {
            // Ocean always visible if camera below threshold altitude
            return cameraPos.z() < 1000.0f;
        } else {
            // Lake: Check bounding box distance
            return extent.intersects(osg::BoundingSphere(cameraPos, maxDist));
        }
    }
};
```

**Pros:**
- ✅ Ocean always available when needed
- ✅ Simple culling logic

**Cons:**
- ⚠️ Need to prevent ocean rendering above mountains

---

### 3.4 Rendering Strategy for Multiple Water Bodies

#### Challenge: Render Ocean + Multiple Lakes with Correct Depth Sorting

**Issues:**
1. Water is typically rendered with transparency (blend mode)
2. Need proper depth sorting between water bodies
3. Ocean clipmap is camera-centric, lakes are world-space
4. Reflection/refraction RTT cameras need to handle multiple planes

---

#### Solution A: Separate Render Bins by Altitude

**Approach:**
```cpp
void WaterManager::updateVisibleWaterBodies(const osg::Vec3f& cameraPos) {
    auto visibleBodies = mRegistry.getVisibleBodies(cameraPos, 50000.0f);

    // Sort by altitude (lowest first)
    std::sort(visibleBodies.begin(), visibleBodies.end(),
        [](const auto& a, const auto& b) {
            return a->descriptor.height < b->descriptor.height;
        });

    // Assign render bins based on altitude order
    int renderBin = RenderBin_Water;
    for (auto* body : visibleBodies) {
        body->implementation->setRenderBin(renderBin);
        renderBin += 1;  // Higher altitude = higher render bin (renders later)
    }
}
```

**Pros:**
- ✅ Correct depth sorting
- ✅ OpenMW already uses render bins

**Cons:**
- ⚠️ Limited render bin range
- ⚠️ May conflict with other systems

---

#### Solution B: Ocean Height Clipping

**Approach:** Only render ocean where terrain elevation is below threshold

```glsl
// ocean.vert
uniform float terrainMaxElevation;  // Set per-frame based on nearby terrain

void main() {
    vec3 worldPos = vertPos + vec3(snappedCameraPos, 0.0) + totalDisplacement;

    // Sample terrain height at this position
    float terrainHeight = sampleTerrainHeight(worldPos.xy);

    // Clip ocean vertices above terrain threshold
    if (terrainHeight > terrainMaxElevation || worldPos.z > terrainMaxElevation) {
        gl_Position = vec4(0, 0, 0, 0);  // Discard vertex
        return;
    }

    // ... rest of shader
}
```

**Pros:**
- ✅ Prevents ocean rendering on mountains
- ✅ Clean separation between ocean and elevated lakes

**Cons:**
- ❌ Requires terrain height sampling (expensive)
- ❌ May create artifacts at boundaries

---

#### Solution C: Distance-Based Culling (Recommended)

**Approach:**
```cpp
void WaterBodyInstance::updateVisibility(const osg::Vec3f& cameraPos) {
    if (descriptor.infinite) {
        // Ocean: Visible if camera below altitude threshold
        float maxOceanVisibleAltitude = descriptor.height + 500.0f;
        visible = cameraPos.z() < maxOceanVisibleAltitude;
    } else {
        // Lake: Visible if within distance and altitude range
        float dist = extent.distance(cameraPos);
        float maxDist = 20000.0f;  // 20km visibility

        visible = (dist < maxDist);

        // Additional: Don't render lakes below camera if far away
        if (descriptor.height < cameraPos.z() - 200.0f && dist > 5000.0f)
            visible = false;
    }
}
```

**Pros:**
- ✅ Simple, performant
- ✅ Handles most cases
- ✅ Prevents overlapping water bodies

**Cons:**
- ⚠️ Not perfect (edge cases remain)

---

### 3.5 Swimming Animation Trigger

**Requirement:** All water bodies must trigger swimming animation when player enters.

#### Current Implementation

**File:** [physicssystem.cpp](apps/openmw/mwphysics/physicssystem.cpp) (search "isUnderwater")

**Current Logic:**
```cpp
bool PhysicsSystem::isUnderwater(const osg::Vec3f& pos) const {
    return pos.z() < mWaterHeight;  // Single global water height
}
```

**Problem:** Only one water height checked.

---

#### Proposed Solution

**Modify PhysicsSystem:**
```cpp
class PhysicsSystem {
    std::vector<WaterPlane> mWaterPlanes;  // NEW: Multiple water planes

    void enableWater(float height, const osg::BoundingBox& extent);
    void clearWater();
    bool isUnderwater(const osg::Vec3f& pos) const;
};

bool PhysicsSystem::isUnderwater(const osg::Vec3f& pos) const {
    for (const auto& plane : mWaterPlanes) {
        // Check if position is below water surface AND within bounds
        if (pos.z() < plane.height && plane.extent.contains(pos))
            return true;
    }
    return false;
}
```

**Integration:**
```cpp
// WaterManager::updateVisibleWaterBodies()
void WaterManager::updateVisibleWaterBodies(const osg::Vec3f& cameraPos) {
    mPhysics->clearWater();

    auto visibleBodies = mRegistry.getVisibleBodies(cameraPos, 50000.0f);
    for (auto* body : visibleBodies) {
        if (body->visible) {
            mPhysics->enableWater(body->descriptor.height, body->descriptor.extent);
        }
    }
}
```

---

### 3.6 Reflection/Refraction for Multiple Water Bodies

**Challenge:** Current RTT cameras assume single water plane.

#### Option A: One RTT Per Water Body (Expensive)

**Approach:**
```cpp
class WaterBodyInstance {
    std::unique_ptr<Refraction> mRefraction;
    std::unique_ptr<Reflection> mReflection;
};
```

**Pros:**
- ✅ Accurate per-body reflections

**Cons:**
- ❌ Expensive (2 RTT passes per water body)
- ❌ Doesn't scale to many lakes

---

#### Option B: Shared RTT, Dominant Water Body (Recommended)

**Approach:**
```cpp
void WaterManager::updateRTT(const osg::Vec3f& cameraPos) {
    // Use reflection/refraction from closest or largest water body
    WaterBodyInstance* dominant = findDominantWaterBody(cameraPos);

    if (dominant) {
        float height = dominant->descriptor.height;
        mRefraction->setWaterLevel(height);
        mReflection->setWaterLevel(height);
    }
}

WaterBodyInstance* WaterManager::findDominantWaterBody(const osg::Vec3f& cameraPos) {
    // Strategy 1: Closest water body
    // Strategy 2: Largest visible water body
    // Strategy 3: Water body camera is in/above

    WaterBodyInstance* closest = nullptr;
    float minDist = FLT_MAX;

    for (auto& body : mRegistry.mBodies) {
        if (!body.visible) continue;

        float dist = body.descriptor.extent.distance(cameraPos);
        if (dist < minDist) {
            minDist = dist;
            closest = &body;
        }
    }

    return closest;
}
```

**Pros:**
- ✅ Only 2 RTT passes total (like current)
- ✅ Good approximation for most cases

**Cons:**
- ⚠️ Reflections may be wrong for distant lakes
- ⚠️ Acceptable trade-off

---

#### Option C: No Reflections for Lakes (Performance)

**Approach:**
- Ocean gets full RTT (reflection + refraction)
- Lakes use simple shader (no screen-space effects)

**Pros:**
- ✅ Best performance
- ✅ Ocean (main feature) looks great

**Cons:**
- ⚠️ Lakes look flat
- ⚠️ May be acceptable if lakes are small

---

### 3.7 Implementation Phases

#### Phase 1: Core Multi-Water Architecture (Week 1-2)

**Goals:**
- ✅ Implement WaterBodyRegistry
- ✅ Implement WaterBodyDescriptor
- ✅ Add water type detection (heuristic)
- ✅ Modify WaterManager to use registry
- ✅ Update PhysicsSystem for multiple water planes

**Files to Modify:**
- `water.hpp` / `water.cpp` - Add registry, remove mTop
- `waterbody.hpp` - Extend with descriptor
- `renderingmanager.cpp` - Update setWaterHeight() calls
- `physicssystem.hpp` / `physicssystem.cpp` - Multiple water planes

**Success Criteria:**
- Ocean renders only in ocean cells
- Lakes render in elevated cells
- Both visible simultaneously
- Swimming works in both

---

#### Phase 2: Lake Shader Improvement (Week 2-3)

**Goals:**
- ✅ Implement proper lake fragment shader
- ✅ Add simplified waves (Gerstner waves or texture scrolling)
- ✅ Add reflection/refraction for dominant water body
- ✅ Add basic foam

**Files to Modify:**
- `lake.frag` / `lake.vert` - Full shader implementation
- `water.cpp` - RTT dominant body logic

**Success Criteria:**
- Lakes look like water (not solid blue)
- Lakes have animated surface
- Reflections work reasonably

---

#### Phase 3: Extent & Culling (Week 3-4)

**Goals:**
- ✅ Implement lake extent calculation
- ✅ Implement visibility culling based on bounds
- ✅ Implement ocean altitude clipping
- ✅ Optimize render bin assignment

**Files to Modify:**
- `water.cpp` - Extent calculation, culling logic
- `ocean.cpp` - Altitude-based visibility

**Success Criteria:**
- Lakes don't render when far away
- Ocean doesn't render on mountains
- Performance is good (no overdraw)

---

#### Phase 4: River Support (Future)

**Goals:**
- ✅ Implement River class (inherits WaterBody)
- ✅ Add flow direction parameter
- ✅ Implement directional current shader
- ✅ Detect river cells (heuristic or manual)

**Files to Create:**
- `river.hpp` / `river.cpp`
- `river.frag` / `river.vert`

**Success Criteria:**
- Rivers flow in correct direction
- Water has current effect
- Swimming considers flow

---

## PART 4: RECOMMENDED IMPLEMENTATION PLAN

### 4.1 Immediate Next Steps

**Goal:** Get ocean constrained to ocean cells, lakes visible separately.

#### Step 1: Add WaterBodyRegistry (1-2 days)

**Tasks:**
1. Create `waterbody.hpp` with:
   - `WaterBodyDescriptor` struct
   - `WaterBodyInstance` class
   - `WaterBodyRegistry` class

2. Modify `water.hpp`:
   - Add `WaterBodyRegistry mRegistry`
   - Remove `float mTop` (replace with per-body heights)
   - Remove `bool isOcean` logic

3. Implement detection:
   - Add `detectWaterType()` in `water.cpp`
   - Add `calculateExtent()` in `water.cpp`

**Code Sketch:**
```cpp
// waterbody.hpp (NEW FILE)
#pragma once

enum class WaterType { Ocean, Lake, River };

struct WaterBodyDescriptor {
    WaterType type;
    float height;
    osg::BoundingBox extent;
    bool infinite;
    ESM::RefId cellId;
};

class WaterBodyInstance {
public:
    WaterBodyDescriptor descriptor;
    std::unique_ptr<WaterBody> implementation;
    bool visible;

    void updateVisibility(const osg::Vec3f& cameraPos);
};

class WaterBodyRegistry {
public:
    void registerWaterBody(WaterBodyDescriptor desc);
    void unregisterCell(const ESM::RefId& cellId);
    std::vector<WaterBodyInstance*> getVisibleBodies(const osg::Vec3f& cameraPos);

private:
    std::vector<WaterBodyInstance> mBodies;
    std::map<ESM::RefId, size_t> mCellBodyMap;
};
```

---

#### Step 2: Integrate Registry into WaterManager (2-3 days)

**Tasks:**
1. Modify `WaterManager::changeCell()`:
   - Call `detectWaterType(cell)`
   - Create WaterBodyDescriptor
   - Register with mRegistry

2. Modify `WaterManager::update()`:
   - Call `mRegistry.getVisibleBodies(cameraPos)`
   - Update each visible body
   - Hide invisible bodies

3. Remove old logic:
   - Delete `setHeight(float)` (replace with per-body)
   - Delete `isOcean` boolean

**Code Sketch:**
```cpp
// water.cpp
void WaterManager::changeCell(const MWWorld::CellStore* store) {
    const Cell& cell = *store->getCell();

    if (!cell.hasWater())
        return;

    // Detect water type
    WaterType type = detectWaterType(store);

    // Create descriptor
    WaterBodyDescriptor desc;
    desc.type = type;
    desc.height = cell.getWaterHeight();
    desc.cellId = cell.getId();
    desc.infinite = (type == WaterType::Ocean);
    desc.extent = calculateExtent(store, type);

    // Register
    mRegistry.registerWaterBody(desc);
}

WaterType WaterManager::detectWaterType(const MWWorld::CellStore* store) {
    const Cell& cell = *store->getCell();

    if (!cell.isExterior())
        return WaterType::Lake;

    float waterHeight = cell.getWaterHeight();
    const float SEA_LEVEL = 0.0f;
    const float TOLERANCE = 5.0f;

    if (std::abs(waterHeight - SEA_LEVEL) <= TOLERANCE)
        return WaterType::Ocean;  // TODO: Add connectivity check

    return WaterType::Lake;
}

osg::BoundingBox WaterManager::calculateExtent(const MWWorld::CellStore* store, WaterType type) {
    if (type == WaterType::Ocean)
        return osg::BoundingBox();  // Infinite

    // Cell-based bounds
    float cellSize = Constants::CellSizeInUnits;
    int x = store->getCell()->getGridX();
    int y = store->getCell()->getGridY();
    float h = store->getCell()->getWaterHeight();

    return osg::BoundingBox(
        osg::Vec3f(x * cellSize, y * cellSize, h - 10),
        osg::Vec3f((x+1) * cellSize, (y+1) * cellSize, h + 100)
    );
}
```

---

#### Step 3: Update Ocean Visibility Logic (1 day)

**Tasks:**
1. Modify `Ocean::update()`:
   - Add altitude-based culling
   - Only render if camera below threshold

2. Add ocean height limit check

**Code Sketch:**
```cpp
// ocean.cpp
void Ocean::update(float dt, bool paused, const osg::Vec3f& cameraPos) {
    if (!mEnabled) return;

    // Ocean culling: Don't render if camera far above sea level
    const float OCEAN_MAX_VISIBLE_ALTITUDE = mHeight + 500.0f;
    if (cameraPos.z() > OCEAN_MAX_VISIBLE_ALTITUDE) {
        mRootNode->setNodeMask(0);
        return;
    }

    mRootNode->setNodeMask(~0u);

    // ... rest of update
}
```

---

#### Step 4: Fix PhysicsSystem for Multi-Water (1 day)

**Tasks:**
1. Add `std::vector<WaterPlane> mWaterPlanes` to PhysicsSystem
2. Modify `isUnderwater()` to check all planes
3. Add `addWaterPlane()` / `clearWaterPlanes()` methods

**Code Sketch:**
```cpp
// physicssystem.hpp
struct WaterPlane {
    float height;
    osg::BoundingBox extent;
};

class PhysicsSystem {
    std::vector<WaterPlane> mWaterPlanes;
public:
    void addWaterPlane(float height, const osg::BoundingBox& extent);
    void clearWaterPlanes();
    bool isUnderwater(const osg::Vec3f& pos) const override;
};

// physicssystem.cpp
bool PhysicsSystem::isUnderwater(const osg::Vec3f& pos) const {
    for (const auto& plane : mWaterPlanes) {
        bool belowSurface = pos.z() < plane.height;
        bool inBounds = plane.extent.contains(pos);

        if (belowSurface && inBounds)
            return true;
    }
    return false;
}
```

---

### 4.2 Testing Plan

#### Test Case 1: Ocean Only (Baseline)

**Setup:**
- Player in coastal cell (Seyda Neen)
- Water height = 0.0

**Expected:**
- ✅ Ocean renders with FFT waves
- ✅ Ocean extends to horizon
- ✅ Swimming works
- ✅ Reflections work

---

#### Test Case 2: Lake Only

**Setup:**
- Player near elevated lake (e.g., Sheogorad region)
- Water height = 512.0

**Expected:**
- ✅ Lake renders (no ocean)
- ✅ Lake bounded to cell extent
- ✅ Swimming works
- ✅ Lake shader active

---

#### Test Case 3: Ocean + Lake Simultaneously

**Setup:**
- Player on hill between coast and mountain lake
- Ocean at 0.0, Lake at 512.0

**Expected:**
- ✅ Both visible
- ✅ No overlap
- ✅ Swimming works in both
- ✅ Correct depth sorting

---

#### Test Case 4: Multiple Lakes

**Setup:**
- Multiple lakes at different altitudes (128, 256, 512)

**Expected:**
- ✅ All visible when in range
- ✅ Each at correct altitude
- ✅ Swimming works in all
- ✅ No z-fighting

---

### 4.3 Potential Risks & Mitigations

#### Risk 1: Performance Degradation

**Concern:** Multiple water bodies = multiple draw calls

**Mitigation:**
- Use aggressive culling (distance + frustum)
- Limit visible water bodies (e.g., max 5 at once)
- Use LOD for distant lakes (simpler geometry)

---

#### Risk 2: Reflection/Refraction Artifacts

**Concern:** RTT cameras may show wrong reflections

**Mitigation:**
- Use "dominant water body" approach
- Accept that distant lakes have approximate reflections
- Document as known limitation

---

#### Risk 3: Z-Fighting Between Water Bodies

**Concern:** Overlapping water at similar altitudes

**Mitigation:**
- Use render bin sorting by altitude
- Add small Z-offset per water body (0.1 units)
- Ensure proper depth testing

---

#### Risk 4: Mod Compatibility

**Concern:** Mods may add custom water cells

**Mitigation:**
- Ensure detection algorithm is robust
- Add manual override system (settings file)
- Provide modding documentation

---

## PART 5: CONCLUSION & RECOMMENDATIONS

### 5.1 Summary of Findings

**Architecture:** ✅ Well-designed, extensible (WaterBody interface)

**Ocean Implementation:** ✅ Excellent FFT system, ~90% complete

**Current Limitation:** ❌ Single global water height, binary ocean/lake switch

**Root Cause:** Missing per-cell water body registry with spatial extent

---

### 5.2 Recommended Solution

**Implement Multi-Body Water Registry:**

1. **WaterBodyRegistry** - Manages multiple concurrent water bodies
2. **Water Type Detection** - Heuristic-based (altitude + perimeter check)
3. **Extent Calculation** - Cell-based bounds for lakes, infinite for ocean
4. **Visibility Culling** - Distance and altitude-based
5. **Physics Integration** - Multiple water planes for swimming

---

### 5.3 Estimated Effort

**Phase 1 (Core Multi-Water):** 1-2 weeks
- Registry implementation
- Detection algorithm
- WaterManager refactor
- Physics updates

**Phase 2 (Lake Shader):** 1 week
- Lake fragment shader
- Basic animations
- RTT integration

**Phase 3 (Polish):** 1 week
- Extent optimization
- Culling refinement
- Performance testing

**Total:** 3-4 weeks for full multi-altitude water support

---

### 5.4 Priority Recommendations

**Must-Have (P0):**
- ✅ WaterBodyRegistry
- ✅ Water type detection
- ✅ Multiple water bodies rendering
- ✅ Swimming in all water types

**Should-Have (P1):**
- ✅ Lake shader improvement (beyond solid blue)
- ✅ Proper extent calculation and culling
- ✅ Ocean altitude clipping

**Nice-to-Have (P2):**
- ⚪ Per-lake reflections
- ⚪ Connectivity-based ocean detection
- ⚪ River support

---

## APPENDICES

### Appendix A: File Locations Quick Reference

| Component | File | Lines |
|-----------|------|-------|
| WaterManager | `apps/openmw/mwrender/water.hpp` | 57-158 |
| WaterManager | `apps/openmw/mwrender/water.cpp` | 1-1065 |
| Ocean | `apps/openmw/mwrender/ocean.hpp` | 29-127 |
| Ocean | `apps/openmw/mwrender/ocean.cpp` | 1-1173 |
| Lake | `apps/openmw/mwrender/lake.hpp` | 22-49 |
| Lake | `apps/openmw/mwrender/lake.cpp` | 1-125 |
| WaterBody Interface | `apps/openmw/mwrender/waterbody.hpp` | 15-28 |
| Cell Data | `apps/openmw/mwworld/cell.hpp` | 42-49 |
| Scene Integration | `apps/openmw/mwworld/scene.cpp` | 501-516 |

### Appendix B: Key Constants

```cpp
const float MW_CELL_SIZE = 8192.0f;           // Morrowind units
const float SEA_LEVEL = 0.0f;                 // Default ocean height
const float SEA_LEVEL_TOLERANCE = 5.0f;       // ±5 units for ocean detection
const float OCEAN_MAX_ALTITUDE = 500.0f;      // Don't render ocean above this
const float LAKE_VISIBILITY_DIST = 20000.0f;  // Max lake render distance
```

### Appendix C: Morrowind World Geography

**Typical Water Heights:**
- Ocean: 0.0 units
- Harbors/Ports: -5.0 to 5.0 units
- Vivec Canals: 0.0 units (at sea level, but should be lake)
- Ashlands Pools: 128.0 to 256.0 units
- Mountain Lakes: 512.0 to 1024.0 units
- Underground Caverns: -256.0 to 0.0 units

---

**End of Document**

---

**Next Actions:**
1. Review this document with stakeholders
2. Get approval on architecture approach
3. Begin Phase 1 implementation (WaterBodyRegistry)
4. Create unit tests for water type detection
5. Update OCEAN_IMPLEMENTATION_TRACKING.md with new tasks
