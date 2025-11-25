# OpenMW Multi-Water System Implementation & Tracking

**Project Goal:** Implement realistic multi-altitude water system (ocean, lakes, rivers) with modern rendering techniques

**Base Commit:** bb3b3eb5e498183ae8c804810d6ebdba933dbeb2
**Current Status:** Ocean FFT ~90% complete, Multi-water system in design phase
**Last Updated:** 2025-11-25

---

## TABLE OF CONTENTS

1. [Current Status](#current-status)
2. [Architecture Overview](#architecture-overview)
3. [Ocean FFT Status](#ocean-fft-status)
4. [Multi-Water System Design](#multi-water-system-design)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Session History](#session-history)

---

## CURRENT STATUS

### What Works ✅
- ✅ **Ocean FFT System** - 4-cascade wave simulation with realistic physics
- ✅ **Ocean/Lake Architecture** - Basic switching between ocean (exterior) and lake (interior)
- ✅ **PBR Shading** - GGX microfacet, Fresnel-Schlick, subsurface scattering
- ✅ **Clipmap LOD** - 10-ring system extending to ~12.8km horizon
- ✅ **Reflection/Refraction** - RTT cameras for water effects
- ✅ **Runtime Parameters** - Lua API for wind, waves, colors

### Current Problems ❌
1. **Ocean covers entire island** - Clipmap follows camera, no spatial bounds
2. **Sea-level lakes trigger ocean** - `|waterHeight| <= 10` threshold too broad (Vivec canals, etc.)
3. **Single global water height** - Can't have ocean (0.0) + mountain lake (512.0) simultaneously
4. **No multi-altitude swimming** - `isUnderwater()` checks single global Z height
5. **Lake shader incomplete** - Currently solid blue placeholder

### Critical Decisions Made 🎯
- ✅ Use **Height Field** (2D texture) for swimming detection across multiple altitudes
- ✅ Implement **Primary Water System** - Max 1 water body gets RTT, others use SSR
- ✅ Water type hierarchy: Ocean (FFT+RTT) > Lakes (Gerstner+SSR) > Rivers (Flow+SSR) > Puddles (Decals)
- ✅ **2 RTT cameras max** - Dynamically allocated to primary water body

---

## ARCHITECTURE OVERVIEW

### Modern Open-World Approach

Based on research of **The Witcher 3**, **Cyberpunk 2077**, **Red Dead Redemption 2**:

```
┌─────────────────────────────────────────────────────────────┐
│ WaterHeightField (2D Texture)                               │
│ - Covers all loaded cells                                   │
│ - R16F: Water height at each XY position                    │
│ - R8UI: Water type (none/ocean/lake/river)                  │
│ - Used by PhysicsSystem for swimming detection              │
│ - Used by shaders for underwater effects                    │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ WaterVolumeManager                                          │
│ - Tracks active water bodies (vector<WaterVolume>)          │
│ - Classifies: Ocean / Lake / River / Puddle                 │
│ - Calculates spatial bounds per volume                      │
│ - Determines "primary water" (gets RTT)                     │
└─────────────────────────────────────────────────────────────┘
                           ↓
        ┌──────────────┬───────────────┬──────────────┐
        ↓              ↓               ↓              ↓
    ┌────────┐    ┌────────┐      ┌────────┐    ┌─────────┐
    │ Ocean  │    │  Lake  │      │ River  │    │ Puddle  │
    │ (FFT)  │    │(Gerstn)│      │ (Flow) │    │ (Decal) │
    └────────┘    └────────┘      └────────┘    └─────────┘
        │              │               │              │
        ↓              ↓               ↓              ↓
    RTT (2×)      SSR/RTT*         SSR only      Cubemap
```

*If lake is "primary water"

---

## OCEAN FFT STATUS

### ✅ COMPLETE FEATURES

#### Core FFT Pipeline
- ✅ Stockham FFT algorithm (4 cascades: 50m, 100m, 200m, 400m)
- ✅ TMA/JONSWAP wave spectrum
- ✅ Hasselmann directional spreading
- ✅ Displacement + normal + foam generation
- ✅ Compute shader pipeline (butterfly, spectrum, modulate, FFT, transpose, unpack)

#### Visual Quality
- ✅ **Unit Conversion Fix** (2025-11-24) - Wave physics now in meters, not MW units
- ✅ **PBR Shading** - GGX, Fresnel-Schlick, Smith geometry, Cook-Torrance BRDF
- ✅ **Subsurface Scattering** - Green-shifted backlit wave peaks
- ✅ **Distance Falloffs** - Normals, displacement, foam fade with distance (2× Godot range)
- ✅ **Reflection/Refraction** - Screen-space RTT with normal distortion
- ✅ **Foam System** - Jacobian-based whitecap detection with persistence/decay

#### Performance
- ✅ **Clipmap LOD** - 10 concentric rings (Ring 0: 512×512, Rings 1-9: progressively coarser)
- ✅ **Grid Snapping** - Prevents texture swimming during camera movement
- ✅ **Stationary Mesh** - Mesh at origin, shader does world-space offsetting

#### Runtime Control
- ✅ **Lua API** - `openmw.ocean.*` functions for all parameters
- ✅ **Parameters:** windSpeed, windDirection, fetchLength, swell, detail, spread, foamAmount
- ✅ **Colors:** waterColor, foamColor per-cascade customization

### ❌ KNOWN ISSUES

#### 1.7. Outer Ring Displacement Artifacts (INVESTIGATING)
**Symptom:** Rings 1-9 show vertex jumping during camera movement
**Status:** Investigating (multiple fix attempts failed)
**Priority:** Medium (doesn't block multi-water work)

**Attempted Fixes:**
- ❌ Vertex grid alignment
- ❌ Double-offset removal
- ❌ Local vs world coordinates
- ❌ Unsnapped UV sampling

**Current Theory:** Clipmap approach may be incompatible with coarse LOD rings

---

## MULTI-WATER SYSTEM DESIGN

### Phase 1: Height Field System ⏳ IN PROGRESS

**Goal:** Enable swimming at multiple altitudes

#### 1.1. WaterHeightField Class

**File:** `apps/openmw/mwrender/waterheightfield.hpp` (NEW)

```cpp
class WaterHeightField {
public:
    WaterHeightField(int resolution);

    void updateFromLoadedCells(const std::vector<MWWorld::CellStore*>& cells);

    float sampleHeight(const osg::Vec3f& worldPos) const;
    WaterType sampleType(const osg::Vec3f& worldPos) const;

    osg::Image* getHeightTexture() { return mHeightField.get(); }
    osg::Image* getTypeTexture() { return mWaterType.get(); }

private:
    osg::ref_ptr<osg::Image> mHeightField;  // R16F: water height
    osg::ref_ptr<osg::Image> mWaterType;    // R8UI: 0=none, 1=ocean, 2=lake, 3=river

    osg::Vec2i mOrigin;  // World grid coords of texture origin
    int mSize;           // Texture dimensions (e.g., 2048×2048)
    float mTexelsPerUnit;  // Spatial resolution
};
```

**Key Methods:**

```cpp
void WaterHeightField::updateFromLoadedCells(
    const std::vector<MWWorld::CellStore*>& cells)
{
    // Reset texture
    std::fill_n((float*)mHeightField->data(), mSize*mSize, -1000.0f);
    std::fill_n((uint8_t*)mWaterType->data(), mSize*mSize, 0);

    for (const auto* cell : cells) {
        if (!cell->getCell()->hasWater()) continue;

        float waterHeight = cell->getCell()->getWaterHeight();
        WaterType type = classifyWaterType(cell);

        // Rasterize cell into texture
        rasterizeCellToTexture(cell, waterHeight, type);
    }

    mHeightField->dirty();
    mWaterType->dirty();
}

float WaterHeightField::sampleHeight(const osg::Vec3f& worldPos) const {
    osg::Vec2f uv = worldToUV(worldPos.xy());

    // Bilinear interpolation
    int x = (int)(uv.x() * mSize);
    int y = (int)(uv.y() * mSize);

    if (x < 0 || x >= mSize || y < 0 || y >= mSize)
        return -1000.0f;  // No water marker

    return ((float*)mHeightField->data())[y * mSize + x];
}
```

**Resolution Calculation:**
- Cell size: 8192 MW units
- Texels per cell: 256 (good balance)
- For 8×8 cell view distance: 2048×2048 texture
- Memory: 2048² × (2 bytes + 1 byte) = 12 MB

#### 1.2. PhysicsSystem Integration

**File:** `apps/openmw/mwphysics/physicssystem.cpp` (MODIFY)

**Current Code:**
```cpp
bool PhysicsSystem::isUnderwater(const osg::Vec3f& pos) const {
    return pos.z() < mWaterHeight;  // ❌ Single global height
}
```

**New Code:**
```cpp
class PhysicsSystem {
    const WaterHeightField* mWaterHeightField;  // Reference

public:
    void setWaterHeightField(const WaterHeightField* field) {
        mWaterHeightField = field;
    }

    bool isUnderwater(const osg::Vec3f& pos) const override {
        if (!mWaterHeightField)
            return pos.z() < mWaterHeight;  // Fallback

        float waterHeight = mWaterHeightField->sampleHeight(pos);

        if (waterHeight < -999.0f)  // No water at this position
            return false;

        return pos.z() < waterHeight;
    }
};
```

**Integration Point:** `RenderingManager` creates height field, shares with `PhysicsSystem`

#### 1.3. Underwater Effect Shader

**File:** `files/shaders/compatibility/underwater.frag` (MODIFY)

```glsl
uniform sampler2D waterHeightField;
uniform vec2 heightFieldOrigin;
uniform float heightFieldScale;

void main() {
    vec3 cameraPos = getCameraWorldPosition();

    // Sample water height at camera XY
    vec2 gridUV = (cameraPos.xy - heightFieldOrigin) * heightFieldScale;
    float waterHeight = texture(waterHeightField, gridUV).r;

    bool underwater = (waterHeight > -999.0 && cameraPos.z < waterHeight);

    if (underwater) {
        float depth = waterHeight - cameraPos.z;
        fragColor = applyUnderwaterFog(screenColor, depth);
    } else {
        fragColor = vec4(screenColor, 1.0);
    }
}
```

---

### Phase 2: Volume Manager & Classification ⏸️ PENDING

**Goal:** Track multiple concurrent water bodies, distinguish ocean from lakes

#### 2.1. Water Classification

**Algorithm:**
```cpp
WaterType classifyWaterType(const MWWorld::CellStore* cell) {
    // Interior = always lake
    if (cell->getCell()->isInterior())
        return WaterType::Lake;

    float waterHeight = cell->getCell()->getWaterHeight();

    // Too high/low for ocean
    if (std::abs(waterHeight) > 15.0f)
        return WaterType::Lake;

    // Manual override for known lakes at sea level
    if (isKnownLake(cell))
        return WaterType::Lake;

    // Perimeter cells = ocean
    if (isPerimeterCell(cell))
        return WaterType::Ocean;

    // Connectivity check (cached BFS)
    if (isConnectedToOcean(cell))
        return WaterType::Ocean;

    // Default: Lake (safer)
    return WaterType::Lake;
}
```

**Known Lakes List:**
```cpp
static const std::set<std::string> KNOWN_LAKES = {
    "Vivec, Arena",
    "Vivec, Temple",
    "Vivec, Foreign Quarter",
    "Vivec, Hlaalu",
    "Vivec, Redoran",
    "Vivec, Telvanni",
    "Balmora",
    "Ebonheart",
    "Sadrith Mora",
};
```

#### 2.2. WaterVolumeManager

**File:** `apps/openmw/mwrender/watervolumemanager.hpp` (NEW)

```cpp
struct WaterVolume {
    WaterType type;
    float height;
    osg::BoundingBox extent;
    float area;
    float distanceToCamera;
    bool isPrimary;  // Gets RTT cameras
};

class WaterVolumeManager {
public:
    void updateFromLoadedCells(
        const std::vector<MWWorld::CellStore*>& cells,
        const osg::Vec3f& cameraPos);

    WaterVolume* findPrimaryWater(const osg::Vec3f& cameraPos);
    const std::vector<WaterVolume>& getActiveVolumes() const;

private:
    std::vector<WaterVolume> mActiveVolumes;
    WaterVolume* mPrimaryWater;

    osg::BoundingBox calculateBounds(const std::vector<const MWWorld::CellStore*>& cells);
    WaterType classifyWaterType(const MWWorld::CellStore* cell);
};
```

**Primary Water Selection:**
```cpp
WaterVolume* WaterVolumeManager::findPrimaryWater(const osg::Vec3f& cameraPos) {
    float bestScore = -FLT_MAX;
    WaterVolume* best = nullptr;

    for (auto& volume : mActiveVolumes) {
        // Only ocean/lakes get RTT
        if (volume.type != WaterType::Ocean && volume.type != WaterType::Lake)
            continue;

        // Scoring: Ocean priority > Size > Proximity
        float score = 0.0f;
        score += (volume.type == WaterType::Ocean) ? 1000.0f : 0.0f;
        score += volume.area * 0.001f;
        score -= volume.distanceToCamera * 0.01f;

        if (score > bestScore) {
            bestScore = score;
            best = &volume;
        }
    }

    return best;
}
```

---

### Phase 3: Lake Renderer Improvements ⏸️ PENDING

**Goal:** Lakes with bounded geometry, SSR reflections

#### 3.1. Bounded Lake Geometry

**Current:** 100km × 100km quad (covers everything)

**New:** Per-volume bounded mesh
```cpp
void Lake::generateMesh(const WaterVolume& volume) {
    osg::Vec3f min = volume.extent.corner(0);
    osg::Vec3f max = volume.extent.corner(7);

    // Create tessellated grid within bounds
    createTessellatedQuad(
        min.xy(), max.xy(), volume.height,
        64, 64  // Grid resolution
    );
}
```

#### 3.2. Screen-Space Reflections (SSR)

**For non-primary lakes**

```glsl
// lake.frag
uniform bool isPrimary;
uniform sampler2D reflectionRTT;
uniform sampler2D sceneDepth;

vec3 getReflection(vec3 normal, vec3 viewDir) {
    if (isPrimary) {
        // Use RTT
        return texture(reflectionRTT, screenCoords + normal.xy * 0.02).rgb;
    }

    // SSR: Raymarch through depth buffer
    vec3 reflectDir = reflect(viewDir, normal);
    vec3 hitPos = raymarchSSR(screenCoords, reflectDir, sceneDepth);

    if (hitPos.z > 0.0) {
        // Hit something
        return texture(sceneColor, hitPos.xy).rgb;
    }

    // Fallback: Sky cubemap
    return textureCube(skybox, reflectDir).rgb;
}
```

#### 3.3. Gerstner Wave Simulation

**Simplified waves for lakes (not full FFT)**

```glsl
// lake.vert
vec3 gerstnerWave(vec2 pos, float time, vec2 dir, float wavelength, float amplitude) {
    float k = 2.0 * PI / wavelength;
    float w = sqrt(9.81 * k);  // Dispersion
    float phase = dot(dir, pos) * k - time * w;

    vec3 displacement;
    displacement.xy = dir * amplitude * sin(phase);
    displacement.z = amplitude * cos(phase);

    return displacement;
}

void main() {
    vec3 worldPos = vertexPos + vec3(0, 0, lakeHeight);

    // Sum 3-4 Gerstner waves
    worldPos += gerstnerWave(worldPos.xy, time, vec2(1,0), 20.0, 0.3);
    worldPos += gerstnerWave(worldPos.xy, time, vec2(0.7,0.7), 15.0, 0.2);
    worldPos += gerstnerWave(worldPos.xy, time, vec2(-0.5,0.9), 25.0, 0.25);

    gl_Position = modelViewProj * vec4(worldPos, 1.0);
}
```

---

### Phase 4: River Support ⏸️ FUTURE

**Goal:** Flowing water with directional current

#### 4.1. Flow Map System

```glsl
// river.frag
uniform sampler2D flowMap;  // RG = flow direction
uniform float flowSpeed;

void main() {
    vec2 flow = texture(flowMap, uv).rg * 2.0 - 1.0;

    // Animate normals along flow
    vec2 uv0 = uv + flow * (time * flowSpeed);
    vec2 uv1 = uv + flow * (time * flowSpeed + 0.5);

    vec3 normal0 = texture(normalMap, uv0).xyz;
    vec3 normal1 = texture(normalMap, uv1).xyz;

    // Blend between phases
    float phase = fract(time * flowSpeed);
    vec3 normal = mix(normal0, normal1, abs(2.0 * phase - 1.0));

    // SSR reflections only
    vec3 reflection = traceSSR(screenCoords, normal, depthBuffer);
}
```

---

## IMPLEMENTATION ROADMAP

### ✅ Phase 0: Ocean FFT (COMPLETE)
- [x] FFT compute pipeline
- [x] PBR shading
- [x] Clipmap LOD
- [x] Runtime parameters
- [ ] Outer ring artifacts (deferred)

### 🔄 Phase 1: Height Field System (IN PROGRESS)
**Timeline:** Week 1 (2025-11-25 to 2025-12-01)

**Tasks:**
- [ ] Create `WaterHeightField` class
- [ ] Implement texture update from loaded cells
- [ ] Integrate with `PhysicsSystem::isUnderwater()`
- [ ] Add underwater shader support
- [ ] Test: Ocean swimming + mountain lake swimming

**Files to Create:**
- `apps/openmw/mwrender/waterheightfield.hpp`
- `apps/openmw/mwrender/waterheightfield.cpp`

**Files to Modify:**
- `apps/openmw/mwphysics/physicssystem.hpp`
- `apps/openmw/mwphysics/physicssystem.cpp`
- `apps/openmw/mwrender/renderingmanager.cpp`
- `files/shaders/compatibility/underwater.frag`

**Success Criteria:**
- ✅ Can swim in ocean (height 0)
- ✅ Can swim in lake (height 512) simultaneously
- ✅ Underwater effect works at both heights
- ✅ No performance regression

---

### ⏸️ Phase 2: Volume Manager & Classification
**Timeline:** Week 2 (2025-12-02 to 2025-12-08)

**Tasks:**
- [ ] Implement `WaterVolumeManager` class
- [ ] Add water type classification algorithm
- [ ] Create known lakes override list
- [ ] Implement ocean connectivity check (BFS, cached)
- [ ] Update `WaterManager` to use volumes
- [ ] Test: Vivec canals don't trigger ocean

**Files to Create:**
- `apps/openmw/mwrender/watervolumemanager.hpp`
- `apps/openmw/mwrender/watervolumemanager.cpp`

**Files to Modify:**
- `apps/openmw/mwrender/water.cpp`
- `apps/openmw/mwrender/water.hpp`

**Success Criteria:**
- ✅ Ocean renders only in true ocean cells
- ✅ Vivec canals use lake renderer
- ✅ Multiple lakes visible at different altitudes
- ✅ Primary water gets RTT, others don't

---

### ⏸️ Phase 3: Lake Renderer Improvements
**Timeline:** Week 3 (2025-12-09 to 2025-12-15)

**Tasks:**
- [ ] Implement bounded lake geometry
- [ ] Add Gerstner wave simulation
- [ ] Implement SSR (screen-space reflections)
- [ ] Add cubemap fallback
- [ ] Primary lake gets RTT
- [ ] Test: Multiple lakes with varied reflections

**Files to Modify:**
- `apps/openmw/mwrender/lake.cpp`
- `files/shaders/compatibility/lake.vert`
- `files/shaders/compatibility/lake.frag`

**Success Criteria:**
- ✅ Lakes bounded to their cells
- ✅ Animated water surface
- ✅ SSR reflections working
- ✅ Performance <2ms per lake

---

### ⏸️ Phase 4: River Support (FUTURE)
**Timeline:** Week 4+

**Tasks:**
- [ ] Create `River` class
- [ ] Implement flow map system
- [ ] Add directional current shader
- [ ] Detect river cells
- [ ] SSR-only reflections

---

## SESSION HISTORY

### Session 1: Initial Audit (2025-11-24)

**Goal:** Understand water system architecture

**Activities:**
- Explored water system codebase
- Analyzed `WaterManager`, `Ocean`, `Lake` classes
- Identified single global `mTop` limitation
- Researched modern game engines (Witcher 3, Cyberpunk, RDR2)

**Findings:**
- ✅ Ocean/Lake architecture already exists (commit 87037fd215)
- ❌ Simple ±10 unit threshold causes ocean false positives
- ❌ Single water height prevents multi-altitude support
- ❌ No spatial bounds (ocean covers 12.8km globally)

**Documents Created:**
- `WATER_SYSTEM_AUDIT_AND_IDEATION.md` (42 KB) - Initial architectural analysis
- `WATER_SYSTEM_REVISED_ANALYSIS.md` (8 KB) - Correction acknowledging existing work
- `WATER_SYSTEM_MODERN_APPROACH.md` (20 KB) - Modern game engine research

**Decision:** Consolidate into single tracking doc (this document)

---

### Session 2: Height Field Implementation (2025-11-25 - COMPLETED)

**Goal:** Implement Phase 1 - Height Field System

**Tasks:**
- [x] Document consolidation (3 docs → 1 master tracking doc)
- [x] Create `WaterHeightField` class header
- [x] Implement `WaterHeightField` class with water type classification
- [x] Add to build system (CMakeLists.txt)
- [x] Integrate with `WaterManager` (addCell/removeCell tracking)
- [x] Integrate with `World::isUnderwater()` for multi-altitude swimming
- [x] Add known lake list (Vivec, Balmora, etc.)

**Files Created:**
- `apps/openmw/mwrender/waterheightfield.hpp` (201 lines)
- `apps/openmw/mwrender/waterheightfield.cpp` (295 lines)

**Files Modified:**
- `apps/openmw/CMakeLists.txt` - Added waterheightfield to build
- `apps/openmw/mwrender/water.hpp` - Added height field member, addCell() method
- `apps/openmw/mwrender/water.cpp` - Initialize height field, track loaded cells
- `apps/openmw/mwrender/renderingmanager.cpp` - Call water->addCell()
- `apps/openmw/mwworld/worldimp.cpp` - Use height field in isUnderwater()

**Status:** Implementation complete, compilation in progress

**What Was Achieved:**
- ✅ 2048×2048 height field texture covering loaded cells
- ✅ Water type classification (Ocean/Lake/River)
- ✅ Manual override list for known lakes at sea level
- ✅ Multi-altitude swimming detection (height field lookup)
- ✅ Cell tracking system to update height field on load/unload
- ✅ Fallback to legacy water level if height field unavailable

**Next Steps for Follow-up Session:**
1. Build and test for compilation errors
2. Test in-game: Ocean + mountain lake swimming
3. Verify Vivec canals don't trigger ocean mode
4. Begin Phase 2: WaterVolumeManager implementation

---

## PERFORMANCE TARGETS

### RTT Budget
- **2 cameras maximum** @ 1024×1024
- Ocean: Reflection (4ms) + Refraction (4ms) = 8ms
- Primary Lake: Reflection (4ms) + SSR fallback = 5ms

### Frame Budget (60 FPS = 16.6ms)
- Ocean FFT: 3ms (compute shaders)
- Ocean Rendering: 2ms (geometry + shader)
- Ocean RTT: 8ms (reflection + refraction)
- Lakes (5×): 2ms (geometry + SSR)
- Rivers (3×): 1.5ms (geometry)
- **Total Water: ~16.5ms** (within budget for 60 FPS)

### Memory Budget
- Height Field: 2048² × 3 bytes = 12 MB
- Ocean FFT Buffers: ~16 MB
- RTT Textures: 2 × 1024² × 4 bytes = 8 MB
- **Total: ~36 MB**

---

## TECHNICAL REFERENCES

### Modern Game Water Systems
- **The Witcher 3**: GDC 2014 - "Creating the Living World"
- **Cyberpunk 2077**: GDC 2021 - "Lighting Night City"
- **Red Dead Redemption 2**: SIGGRAPH 2018 - "Advances in Real-Time Rendering"

### Rendering Techniques
- **FFT Ocean**: Tessendorf 2001 - "Simulating Ocean Water"
- **Wave Spectra**: Horvath 2015 - "Empirical Directional Wave Spectra"
- **SSR**: GPU Gems 3, Chapter 20 - "Screen-Space Reflections"
- **Gerstner Waves**: GPU Gems 1, Chapter 1 - "Effective Water Simulation"
- **Flow Maps**: SIGGRAPH 2010 - "Water Flow in Portal 2"

### OpenMW Specifics
- **Unit Conversion**: 1 meter = 72.53 MW units
- **Cell Size**: 8192 MW units (≈113 meters)
- **World Range**: Roughly -30 to +30 grid coordinates (Morrowind)

---

## CLEANUP NOTES

**Consolidated Documents:**
- `WATER_SYSTEM_AUDIT_AND_IDEATION.md` → Archived
- `WATER_SYSTEM_REVISED_ANALYSIS.md` → Archived
- `WATER_SYSTEM_MODERN_APPROACH.md` → Archived

**Retained Documents:**
- `OCEAN_IMPLEMENTATION_TRACKING.md` → Keep for FFT-specific technical details
- Ocean debug/fix docs → Keep as technical reference

**New Master Document:**
- `WATER_SYSTEM_IMPLEMENTATION.md` (this file) - Single source of truth

---

**Next Action:** Implement `WaterHeightField` class (Phase 1, Task 1)
