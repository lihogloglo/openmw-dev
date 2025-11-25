# Ocean Masking Implementation Plan

## Problem
Ocean at z=0 covers the entire map, including areas where lakes exist at different altitudes. This makes it impossible to see or test lake-specific features like SSR reflections.

## Current Workaround (TEMPORARY)
`mUseOcean = false` in [water.cpp:461](apps/openmw/mwrender/water.cpp#L461)
- Ocean is completely disabled
- Lakes work everywhere
- **For testing only** - not acceptable for release

---

## Proper Solution: Smart Ocean Masking

### Architecture Overview
Use the **WaterHeightField** system to spatially mask ocean rendering:

```
WaterHeightField (2048×2048 texture)
├─ R16F: Water height per texel
└─ R8UI: Water type (None/Ocean/Lake/River)

Ocean Shader
├─ Samples WaterHeightField at fragment position
├─ Discard fragments where type != Ocean
└─ Result: Ocean only renders in true ocean regions
```

### Implementation Steps

#### 1. Pass WaterHeightField to Ocean Shader
**File:** `apps/openmw/mwrender/ocean.cpp`

In `Ocean::initShaders()`:
```cpp
// Add uniforms for water type masking
stateset->addUniform(new osg::Uniform("waterTypeTexture", WATER_TYPE_UNIT));
stateset->addUniform(new osg::Uniform("waterHeightFieldOrigin", mWaterHeightField->getOrigin()));
stateset->addUniform(new osg::Uniform("waterHeightFieldScale", mWaterHeightField->getTexelsPerUnit()));
```

In `Ocean::update()`:
```cpp
// Bind water type texture
stateset->setTextureAttributeAndModes(WATER_TYPE_UNIT,
    mWaterManager->getWaterHeightField()->getTypeTexture(),
    osg::StateAttribute::ON);
```

#### 2. Add Masking to Ocean Fragment Shader
**File:** `files/shaders/ocean_fragment.glsl` (or wherever ocean shader is)

```glsl
uniform sampler2D waterTypeTexture;
uniform vec2 waterHeightFieldOrigin; // Grid coordinates
uniform float waterHeightFieldScale;  // Texels per unit

const int WATER_TYPE_NONE = 0;
const int WATER_TYPE_OCEAN = 1;
const int WATER_TYPE_LAKE = 2;
const int WATER_TYPE_RIVER = 3;

void main() {
    // ... existing ocean shader code ...

    // Sample water type at fragment world position
    vec2 worldPos = worldPosition.xy; // From vertex shader
    vec2 gridPos = worldPos / 8192.0; // Convert to cell coordinates
    vec2 uv = (gridPos - waterHeightFieldOrigin) * waterHeightFieldScale;

    // Discard if not ocean
    int waterType = int(texture2D(waterTypeTexture, uv).r * 255.0);
    if (waterType != WATER_TYPE_OCEAN) {
        discard;
    }

    // ... rest of ocean rendering ...
}
```

#### 3. Update WaterManager Logic
**File:** `apps/openmw/mwrender/water.cpp`

Remove the `isOcean = (std::abs(mTop) <= 10.0f)` hack and use:

```cpp
void WaterManager::changeCell(const MWWorld::CellStore* store)
{
    bool isInterior = !store->getCell()->isExterior();

    if (!isInterior)
    {
        // Exterior: Always enable both ocean and lake
        // They'll self-mask based on WaterHeightField
        if (mUseOcean && mOcean)
            mOcean->setEnabled(mEnabled);

        if (mLake)
            mLake->setEnabled(mEnabled);
    }
    else
    {
        // Interior: Only lake
        if (mLake)
            mLake->setEnabled(mEnabled);
        if (mUseOcean && mOcean)
            mOcean->setEnabled(false);
    }

    // ... cubemap creation etc ...
}
```

#### 4. Refine WaterHeightField Classification
**File:** `apps/openmw/mwrender/waterheightfield.cpp`

Improve `classifyWaterType()` heuristics:

```cpp
WaterType WaterHeightField::classifyWaterType(const MWWorld::CellStore* cell) const
{
    if (!cell->getCell()->isExterior())
        return WaterType::Lake; // All interior water is lake

    float waterHeight = cell->getCell()->getWater();

    // Ocean detection (multiple heuristics)
    if (std::abs(waterHeight) < 10.0f) {
        // Near sea level
        if (isPerimeterCell(cell)) return WaterType::Ocean;
        if (isKnownLake(cell)) return WaterType::Lake;

        // Check connectivity to ocean (requires pathfinding - advanced)
        // For now, default to ocean if near sea level
        return WaterType::Ocean;
    }

    // Above/below sea level = Lake
    if (waterHeight > 10.0f || waterHeight < -10.0f)
        return WaterType::Lake;

    // TODO: River detection (requires flow analysis)
    return WaterType::Lake;
}
```

---

## Performance Considerations

### Texture Sampling Overhead
- **Cost:** 1 additional texture lookup in ocean fragment shader
- **Mitigation:** WaterTypeTexture is R8UI (tiny, cache-friendly)
- **Impact:** Negligible (~0.1ms @ 4K resolution)

### Discard Performance
- **Concern:** Fragment discard can hurt GPU occupancy
- **Reality:** Only discards at lake boundaries (tiny % of fragments)
- **Verdict:** Not a concern

### Alternative: CPU-Side Culling
Instead of shader discard, cull ocean tiles on CPU:
- Generate ocean mesh tiles (e.g., 512×512 unit chunks)
- Sample WaterHeightField per tile
- Only render tiles that contain ocean

**Pros:** Better GPU occupancy
**Cons:** Complex mesh management, more draw calls
**Verdict:** Shader discard is simpler and fast enough

---

## Testing Plan

1. **Enable Ocean:** Set `mUseOcean = true`
2. **Verify Ocean Only in Ocean Regions:**
   - Visit Bitter Coast (ocean cells) → Ocean visible
   - Visit Haalu Plantation (inland lake) → Lake visible, NO ocean
3. **Check Transitions:**
   - Walk from ocean to lake → Clean transition
   - No Z-fighting or gaps
4. **Performance Test:**
   - Measure FPS with ocean masking vs. without
   - Should be <5% difference

---

## Edge Cases to Handle

### 1. Ocean-Lake Boundaries
**Problem:** Water at z=0 in perimeter cells, but also inland lakes at z=0
**Solution:** Perimeter cell heuristic + manual lake list

### 2. Underwater Caves
**Problem:** Cave entrances below sea level
**Solution:** Interiors always use Lake, never Ocean

### 3. Solstheim / Bloodmoon
**Problem:** Different world spaces
**Solution:** WaterHeightField is per-worldspace aware

### 4. Water Height Changes
**Problem:** Console commands changing water level
**Solution:** `WaterManager::setHeight()` triggers WaterHeightField update

---

## Estimated Implementation Time
- **Ocean shader changes:** 30 minutes
- **WaterManager integration:** 20 minutes
- **Testing & refinement:** 1 hour
- **Total:** ~2 hours

---

## References
- [WaterHeightField API](apps/openmw/mwrender/waterheightfield.hpp)
- [Ocean Rendering](apps/openmw/mwrender/ocean.cpp)
- [Water System Progress](WATER_SYSTEM_PROGRESS.md)
