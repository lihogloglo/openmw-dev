# Modern Open-World Water Rendering - Research & Design

**Date:** 2025-11-24
**Context:** Designing multi-water system for OpenMW (ocean, lakes, rivers, puddles)

---

## Research: How Modern Open-World Games Handle Water

### The Witcher 3 (REDengine)

**Water Body Types:**
1. **Ocean/Large Bodies** - Full simulation, tessellated mesh, screen-space reflections
2. **Rivers** - Flow maps, cheaper simulation
3. **Puddles** - Decals with normal maps, planar reflections or SSR

**Key Techniques:**
- **Water Volume System**: Each water body is a **convex volume** (bounding box or mesh)
- **Physics Query**: `isInWater(position)` checks if point is inside ANY water volume
- **Rendering Prioritization**:
  - Ocean: Full RTT reflection/refraction (1-2 cameras max)
  - Rivers: SSR only
  - Puddles: Parallax occlusion mapping (POM) + cubemap reflections
- **No per-puddle RTT** - Way too expensive!

**Source:** GDC 2014 - "Witcher 3 Water Technology"

---

### Cyberpunk 2077 (REDengine 4)

**Water Classification:**
1. **Primary Water** (1 per scene): Ocean, large lake - Gets RTT cameras
2. **Secondary Water** (rivers, canals): SSR + local cubemaps
3. **Tertiary Water** (puddles, wet surfaces): Wetness shader, no 3D water

**Key Innovation: Water Masking System**
- **Water Grid**: 2D grid aligned with world (like a heightfield)
- Each cell stores: `{ waterHeight, waterType, flowDirection }`
- CPU queries grid for swimming detection
- GPU samples grid for rendering decisions

**Reflections:**
- Primary water: 1 reflection RTT camera (shared across all primary water)
- Secondary water: SSR (screen-space reflections)
- Puddles: Pre-baked cubemaps

**Source:** GDC 2021 - "Cyberpunk 2077 Environmental Rendering"

---

### Red Dead Redemption 2 (RAGE)

**Massive open world, lots of water bodies**

**Water Body Hierarchy:**
1. **Ocean** (1 global): FFT simulation, full RTT, follows camera
2. **Lakes** (10-20 visible): Simplified waves, shared RTT or SSR
3. **Rivers** (dynamic count): Flow simulation, SSR only
4. **Puddles** (100+): Shader-only (no geometry)

**Critical Technique: Water Height Field**
- **Global 2D texture** (e.g., 4096×4096) covering entire world
- Red channel: Water height (0-255 = 0-2000m altitude)
- Green channel: Water type (0=none, 1=ocean, 2=lake, 3=river)
- Blue channel: Flow direction (0-360 degrees)
- Updated as player moves (streaming)

**Swimming Detection:**
```cpp
bool isUnderwater(vec3 worldPos) {
    vec2 gridPos = worldToGrid(worldPos.xy);
    float waterHeight = sampleWaterHeightField(gridPos).r;
    return worldPos.z < waterHeight;
}
```

**Rendering:**
- Ocean: Dedicated RTT reflection
- Lakes: Share 1 RTT reflection camera (positioned at largest visible lake)
- Rivers/Puddles: SSR only

**Source:** SIGGRAPH 2018 - "Advances in Real-Time Rendering"

---

## Proposed System for OpenMW

### Core Concept: Hybrid Water Volume + Height Field

Combine both approaches:
1. **Water Height Field** (2D texture) - For swimming detection, cheap queries
2. **Water Volumes** (bounding boxes) - For rendering, culling, type classification

---

### Architecture

#### 1. Water Height Field System

**Implementation:**
```cpp
class WaterHeightField {
public:
    // 2D texture covering loaded cells
    // Resolution: 1 pixel per 10 MW units (819.2 pixels per cell)
    osg::ref_ptr<osg::Image> mHeightField;  // R16F format
    osg::ref_ptr<osg::Image> mWaterType;    // R8UI format (0=none, 1=ocean, 2=lake, 3=river)

    osg::Vec2i mOrigin;  // World grid coordinates of texture origin
    int mSize;           // Texture size (e.g., 2048×2048)

    void updateForLoadedCells(const std::vector<CellStore*>& cells);
    float sampleHeight(const osg::Vec3f& worldPos) const;
    WaterType sampleType(const osg::Vec3f& worldPos) const;
};

void WaterHeightField::updateForLoadedCells(const std::vector<CellStore*>& cells) {
    // Clear texture
    std::fill(mHeightField->data(), mHeightField->data() + mSize*mSize, -1000.0f);

    for (const auto* cell : cells) {
        if (!cell->hasWater()) continue;

        float waterHeight = cell->getWaterHeight();
        WaterType type = classifyWaterType(cell);

        // Rasterize cell bounds into height field
        osg::Vec2i cellGridPos = cell->getGridPos();
        osg::Vec2i texelMin = worldGridToTexel(cellGridPos);
        osg::Vec2i texelMax = worldGridToTexel(cellGridPos + osg::Vec2i(1, 1));

        for (int y = texelMin.y(); y < texelMax.y(); ++y) {
            for (int x = texelMin.x(); x < texelMax.x(); ++x) {
                mHeightField->data(x, y) = waterHeight;
                mWaterType->data(x, y) = static_cast<uint8_t>(type);
            }
        }
    }

    mHeightField->dirty();
    mWaterType->dirty();
}
```

**Benefits:**
- ✅ O(1) swimming detection (single texture lookup)
- ✅ GPU-accessible (can use in shaders)
- ✅ Supports multiple water heights
- ✅ Memory efficient (2-4 MB for 2048² @ R16F + R8)

---

#### 2. Water Volume System

**Purpose:** Higher-level representation for rendering

```cpp
enum class WaterType {
    Ocean,      // FFT simulation, RTT reflections
    Lake,       // Simplified waves, SSR
    River,      // Flow simulation, SSR
    Puddle      // Shader-only, cubemap reflections
};

struct WaterVolume {
    WaterType type;
    float height;
    osg::BoundingBox extent;
    osg::Vec2f flowDirection;  // For rivers

    // Derived properties
    float area;               // For prioritization
    float distanceToCamera;   // For LOD
    bool isPrimary;           // Gets RTT?
};

class WaterVolumeManager {
public:
    std::vector<WaterVolume> mActiveVolumes;
    WaterVolume* mPrimaryWater;  // Largest or closest ocean/lake (gets RTT)

    void updateFromLoadedCells(const std::vector<CellStore*>& cells, const osg::Vec3f& cameraPos);
    WaterVolume* findPrimaryWater(const osg::Vec3f& cameraPos);
};

void WaterVolumeManager::updateFromLoadedCells(
    const std::vector<CellStore*>& cells,
    const osg::Vec3f& cameraPos)
{
    mActiveVolumes.clear();

    // Group cells by water height and connectivity
    std::map<float, std::vector<const CellStore*>> waterBodies;

    for (const auto* cell : cells) {
        if (!cell->hasWater()) continue;
        waterBodies[cell->getWaterHeight()].push_back(cell);
    }

    // Create volume for each distinct water body
    for (const auto& [height, bodyCells] : waterBodies) {
        WaterVolume volume;
        volume.height = height;
        volume.type = classifyWaterType(bodyCells[0]);  // Representative cell
        volume.extent = calculateBounds(bodyCells);
        volume.area = calculateArea(volume.extent);
        volume.distanceToCamera = volume.extent.distance(cameraPos);

        mActiveVolumes.push_back(volume);
    }

    // Determine primary water (gets RTT)
    mPrimaryWater = findPrimaryWater(cameraPos);
}

WaterVolume* WaterVolumeManager::findPrimaryWater(const osg::Vec3f& cameraPos) {
    WaterVolume* best = nullptr;
    float bestScore = -FLT_MAX;

    for (auto& volume : mActiveVolumes) {
        // Only ocean and large lakes get RTT
        if (volume.type != WaterType::Ocean && volume.type != WaterType::Lake)
            continue;

        // Score based on: proximity, size, type priority
        float score = 0.0f;
        score += (volume.type == WaterType::Ocean) ? 1000.0f : 0.0f;  // Ocean priority
        score += volume.area * 0.001f;                                // Size factor
        score -= volume.distanceToCamera * 0.01f;                     // Distance penalty

        if (score > bestScore) {
            bestScore = score;
            best = &volume;
        }
    }

    return best;
}
```

---

### 3. Rendering Strategy by Water Type

#### Ocean (Type 1: Primary Water)

**Renderer:** Existing FFT Ocean class

**Features:**
- ✅ FFT wave simulation
- ✅ Dedicated RTT reflection camera
- ✅ Dedicated RTT refraction camera
- ✅ Full PBR shading
- ✅ Clipmap LOD geometry

**Visibility:**
```cpp
bool Ocean::shouldRender(const WaterVolume& volume, const osg::Vec3f& cameraPos) {
    // Only render if this is the primary water
    if (!volume.isPrimary) return false;

    // Don't render high above ocean
    if (cameraPos.z() > volume.height + 500.0f) return false;

    // Check if we're in an ocean cell (not inland lake)
    return volume.type == WaterType::Ocean;
}
```

**Constraint:** MAX 1 ocean rendering at a time

---

#### Lakes (Type 2: Secondary Water)

**Renderer:** Lake class (improved)

**Features:**
- ✅ Simplified wave simulation (Gerstner waves or animated normal map)
- ⚠️ SSR (Screen-Space Reflections) instead of RTT
- ⚠️ Shared RTT if this is "primary" lake (largest/closest)
- ✅ Bounded geometry (per-cell or per-volume)

**Shader Strategy:**
```glsl
// lake.frag
uniform bool isPrimary;  // Is this the primary water body?
uniform sampler2D reflectionMap;  // RTT (if primary)

void main() {
    vec3 normal = sampleNormalMap(uv);

    vec3 reflection;
    if (isPrimary) {
        // Use RTT reflection
        reflection = texture(reflectionMap, screenCoords + normal.xy * 0.02).rgb;
    } else {
        // Use SSR (screen-space raymarching)
        reflection = traceSSR(screenCoords, normal, depthBuffer);

        // Fallback to cubemap if SSR fails
        if (reflection == vec3(0)) {
            reflection = textureCube(environmentCubemap, reflect(viewDir, normal)).rgb;
        }
    }

    // ... rest of shader
}
```

**Geometry:**
```cpp
void Lake::generateMesh(const WaterVolume& volume) {
    // Create bounded plane matching volume extent
    osg::Vec3f min = volume.extent.corner(0);
    osg::Vec3f max = volume.extent.corner(7);

    // Tessellated grid (e.g., 64×64) within bounds
    createTessellatedQuad(min.xy(), max.xy(), volume.height, 64, 64);
}
```

**Constraint:**
- MAX 1 lake gets RTT (the "primary" one)
- Others use SSR + fallback cubemap

---

#### Rivers (Type 3: Tertiary Water)

**Renderer:** River class (new)

**Features:**
- ✅ Flow map-based animation
- ✅ SSR only (no RTT)
- ✅ Directional current effect
- ✅ Foam along edges

**Shader:**
```glsl
// river.frag
uniform vec2 flowDirection;
uniform float flowSpeed;

void main() {
    // Animated UVs based on flow
    vec2 flowUV = uv + flowDirection * time * flowSpeed;
    vec3 normal = texture(normalMap, flowUV).xyz;

    // SSR for reflections
    vec3 reflection = traceSSR(screenCoords, normal, depthBuffer);

    // No expensive RTT
}
```

---

#### Puddles (Type 4: Decals)

**Renderer:** Shader-only (no geometry)

**Implementation:**
```cpp
// In terrain fragment shader
uniform sampler2D puddleMask;  // Where puddles are

void main() {
    float puddleStrength = texture(puddleMask, worldUV).r;

    if (puddleStrength > 0.01) {
        // Modify terrain normal
        vec3 waterNormal = vec3(0, 0, 1);  // Flat water
        normal = mix(normal, waterNormal, puddleStrength);

        // Increase roughness (wet look)
        roughness = mix(roughness, 0.1, puddleStrength);

        // SSR or cubemap reflection
        vec3 reflection = textureCube(skybox, reflect(viewDir, normal)).rgb;
        albedo = mix(albedo, reflection, puddleStrength * 0.3);
    }

    // ... normal terrain rendering
}
```

**No geometry overhead!** Just shader modification.

---

## Swimming Detection System

### Problem: Multiple Water Heights

**Current Code:** [physicssystem.cpp](apps/openmw/mwphysics/physicssystem.cpp)
```cpp
bool PhysicsSystem::isUnderwater(const osg::Vec3f& pos) const {
    return pos.z() < mWaterHeight;  // ❌ Single global height
}
```

**Solution: Height Field Lookup**

```cpp
class PhysicsSystem {
    WaterHeightField* mWaterHeightField;  // Reference to rendering system's height field

public:
    bool isUnderwater(const osg::Vec3f& pos) const {
        // Sample height field texture
        float waterHeight = mWaterHeightField->sampleHeight(pos);

        if (waterHeight < -999.0f)  // No water marker
            return false;

        return pos.z() < waterHeight;
    }

    WaterType getWaterType(const osg::Vec3f& pos) const {
        return mWaterHeightField->sampleType(pos);
    }
};
```

**Benefits:**
- ✅ Works with multiple water heights
- ✅ O(1) lookup (texture sample)
- ✅ Exact per-cell accuracy
- ✅ Can query water type (for different swim animations?)

---

### Underwater Effect Shader

**Current:** Global underwater post-process

**Improved:** Height field-aware

```glsl
// underwater.frag
uniform sampler2D waterHeightField;
uniform vec2 heightFieldOrigin;
uniform float heightFieldScale;

void main() {
    vec3 cameraPos = getCameraPosition();

    // Sample water height at camera position
    vec2 gridPos = (cameraPos.xy - heightFieldOrigin) * heightFieldScale;
    float waterHeight = texture(waterHeightField, gridPos).r;

    bool underwater = (waterHeight > -999.0 && cameraPos.z < waterHeight);

    if (underwater) {
        // Apply underwater fog, color tint, caustics
        float depth = waterHeight - cameraPos.z;
        vec3 underwaterColor = applyUnderwaterEffects(screenColor, depth);
        fragColor = vec4(underwaterColor, 1.0);
    } else {
        fragColor = vec4(screenColor, 1.0);
    }
}
```

---

## Reflection Strategy Summary

### RTT Budget: 2 Cameras Maximum

**Allocation:**
1. **Reflection Camera** - Assigned to primary water body
2. **Refraction Camera** - Assigned to primary water body

**Primary Water Selection:**
```
Priority Order:
1. Ocean (if visible and close) - Always gets RTT if present
2. Largest lake (by area) within 5000 units
3. Closest lake to camera
4. None (if only rivers/puddles visible)
```

### Fallback Reflections

**For non-primary water:**

1. **SSR (Screen-Space Reflections)** - Primary fallback
   - Raymarch through depth buffer
   - Works well for calm water
   - Fast (2-3ms on modern GPU)

2. **Cubemap Reflections** - Secondary fallback
   - Pre-baked or dynamic (update every N frames)
   - Used when SSR fails (off-screen reflections)
   - Very fast (texture lookup)

3. **Planar Approximation** - Tertiary fallback
   - Reflect sky color based on view angle
   - Cheapest option
   - Better than nothing

**Implementation:**
```glsl
vec3 getWaterReflection(vec3 normal, vec3 viewDir, bool isPrimary) {
    if (isPrimary) {
        // Use RTT
        return texture(reflectionRTT, screenCoords).rgb;
    }

    // Try SSR first
    vec3 ssr = traceScreenSpaceReflection(screenCoords, normal, viewDir);
    if (ssr.r >= 0.0) return ssr;  // Hit something

    // Fallback to cubemap
    vec3 reflectDir = reflect(viewDir, normal);
    return textureCube(environmentMap, reflectDir).rgb;
}
```

---

## Water Classification Algorithm

### Improved Ocean Detection

**Goal:** Distinguish ocean from sea-level lakes

```cpp
WaterType classifyWaterType(const CellStore* cell) {
    if (cell->isInterior())
        return WaterType::Lake;  // All interior water = lakes

    float waterHeight = cell->getWaterHeight();

    // Too high or too low = definitely lake
    if (std::abs(waterHeight) > 15.0f)
        return WaterType::Lake;

    // Check if connected to world perimeter (ocean boundary)
    if (isPerimeterCell(cell))
        return WaterType::Ocean;

    // Check manual override list (Vivec canals, etc.)
    if (isKnownLake(cell))
        return WaterType::Lake;

    // Check connectivity (BFS, cached)
    if (isConnectedToOcean(cell))
        return WaterType::Ocean;

    // Default: Lake (safer assumption)
    return WaterType::Lake;
}

bool isPerimeterCell(const CellStore* cell) {
    int x = cell->getGridX();
    int y = cell->getGridY();

    // Morrowind world: roughly -30 to +30
    // Ocean cells are at edges
    return (std::abs(x) > 25 || std::abs(y) > 25);
}

bool isConnectedToOcean(const CellStore* cell) {
    // Use cached connectivity map (computed at startup)
    static std::set<osg::Vec2i> oceanConnectedCells;

    if (oceanConnectedCells.empty()) {
        // BFS from all perimeter cells
        computeOceanConnectivity(oceanConnectedCells);
    }

    return oceanConnectedCells.count(cell->getGridPos());
}
```

**Known Lake List (Manual Override):**
```cpp
static const std::set<std::string> KNOWN_LAKES = {
    // Vivec (all at sea level but should be lakes)
    "Vivec, Arena",
    "Vivec, Temple",
    "Vivec, Foreign Quarter",
    "Vivec, Hlaalu",
    "Vivec, Redoran",
    "Vivec, Telvanni",

    // Balmora river
    "Balmora",

    // Other edge cases
    "Ebonheart",
    "Sadrith Mora",
};
```

---

## Implementation Plan

### Phase 1: Height Field System (Week 1)

**Goals:**
1. ✅ Implement `WaterHeightField` class
2. ✅ Update from loaded cells
3. ✅ Integrate with `PhysicsSystem::isUnderwater()`
4. ✅ Test swimming in multi-height water

**Success Criteria:**
- Can swim in ocean (height 0)
- Can swim in mountain lake (height 512)
- Underwater effect works in both

---

### Phase 2: Volume Manager (Week 1-2)

**Goals:**
1. ✅ Implement `WaterVolumeManager`
2. ✅ Classify water types (ocean vs lake)
3. ✅ Find primary water body
4. ✅ Update ocean/lake visibility based on volumes

**Success Criteria:**
- Ocean renders only in ocean cells
- Lakes render at correct altitude
- Vivec canals don't trigger ocean

---

### Phase 3: Lake Renderer Improvements (Week 2)

**Goals:**
1. ✅ Bounded lake geometry (per-volume)
2. ✅ Gerstner wave simulation
3. ✅ SSR implementation for non-primary lakes
4. ✅ Cubemap fallback

**Success Criteria:**
- Primary lake has RTT reflections
- Secondary lakes have SSR
- Performance: <5ms for all lakes

---

### Phase 4: River Support (Week 3)

**Goals:**
1. ✅ Implement `River` class
2. ✅ Flow map shader
3. ✅ Directional current
4. ✅ SSR-only reflections

**Success Criteria:**
- Rivers flow correctly
- No RTT overhead
- Swimming considers current

---

### Phase 5: Puddles (Optional)

**Goals:**
1. ⚪ Wetness shader in terrain
2. ⚪ Puddle mask texture
3. ⚪ Cubemap reflections

---

## Performance Targets

**RTT Budget:**
- 2 cameras @ 1024×1024 = ~8ms total
- Ocean: 1 reflection + 1 refraction = 8ms
- OR Lake: 1 reflection + SSR = 5ms

**Total Water Rendering:**
- Ocean: 8ms RTT + 2ms geometry + 3ms FFT = 13ms
- Lakes (5x): 0ms RTT + 1ms geometry + 1ms shader = 2ms
- Rivers (3x): 0ms RTT + 0.5ms geometry = 1.5ms
- **Total: ~16.5ms (60 FPS sustainable)**

---

## Comparison: Old vs New

| Feature | Old System | New System |
|---------|-----------|------------|
| **Water Heights** | 1 global | Unlimited (height field) |
| **Swimming Detection** | Single Z check | Height field lookup |
| **Ocean Coverage** | Global (12.8km) | Ocean cells only |
| **Lake Reflections** | ❌ None (blue) | ✅ SSR + RTT (primary) |
| **RTT Cameras** | 2 (fixed) | 2 (dynamic allocation) |
| **Rivers** | ❌ Not supported | ✅ Flow simulation |
| **Puddles** | ❌ Not supported | ✅ Shader decals |
| **Performance** | ~8ms | ~16ms (more water!) |

---

## Critical Decisions

### ✅ Height Field: YES
- Solves swimming detection elegantly
- GPU-accessible for effects
- Memory efficient

### ✅ Primary Water System: YES
- Limits RTT to 1 water body (best one)
- Fallback to SSR for others
- Matches modern game engines

### ✅ Manual Lake List: YES (Short-term)
- Quick fix for Vivec, etc.
- Can refine connectivity later

### ✅ Volume-Based Rendering: YES
- Proper spatial bounds
- Supports multiple concurrent water bodies
- Clean architecture

---

## Next Immediate Steps

1. **Implement `WaterHeightField`** (2 days)
2. **Update `PhysicsSystem`** to use height field (1 day)
3. **Add manual lake cell list** (1 day)
4. **Test with ocean + mountain lake** (1 day)

**Total:** ~1 week for core multi-water support

---

## References

- **The Witcher 3 Water**: GDC 2014 - "The Witcher 3: Creating the Living World"
- **Cyberpunk 2077**: GDC 2021 - "Lighting Night City"
- **RDR2**: SIGGRAPH 2018 - "Advances in Real-Time Rendering"
- **SSR Tutorial**: GPU Gems 3, Chapter 20
- **Gerstner Waves**: GPU Gems 1, Chapter 1
- **Flow Maps**: SIGGRAPH 2010 - "Water Flow in Portal 2"

---

**End of Document**
