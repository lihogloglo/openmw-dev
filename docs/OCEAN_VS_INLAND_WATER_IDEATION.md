# Ocean vs Inland Water: Delimitation & Shore Smoothing

## Problem Statement

Morrowind uses a single global water plane at height ~0 for everything. Our new FFT ocean simulation should:
1. **Only render as ocean in actual ocean areas** (not rivers, canals, lakes)
2. **Provide visible agitation in the distance** that smoothly transitions to calm water near shores
3. **Distinguish protected/inland water** (Vivec canals, rivers) from open ocean

---

## Part 1: Ocean vs Inland Water Delimitation

### Current State (waterlevels branch)

The `waterlevels` branch already has foundational work:

1. **WaterType Enum** (`waterheightfield.hpp`):
   ```cpp
   enum class WaterType : uint8_t {
       None = 0,
       Ocean = 1,
       Lake = 2,
       River = 3
   };
   ```

2. **WaterHeightField System**:
   - 2D texture (2048×2048) covering loaded cells
   - R16F for water height per texel
   - R8UI for water type classification
   - Can generate ocean mask texture

3. **Manual Lake Override List**:
   ```cpp
   static const std::set<std::string> KNOWN_LAKE_CELLS = {
       "Vivec, Arena", "Vivec, Temple", ...
       "Balmora", "Ebonheart", "Sadrith Mora", ...
   };
   ```

### Proposed Classification Approaches

#### Approach A: Connectivity-Based Detection (Flood Fill)

**Concept**: Ocean cells are those connected to map edges via sea-level water.

```
Algorithm:
1. Start from all perimeter cells with water at z ≈ 0
2. Flood-fill inward through adjacent cells with same water level
3. Mark filled cells as Ocean, unfilled sea-level cells as Lake/River
```

**Pros**:
- Automatically handles most cases
- No manual overrides needed for new content
- Matches real-world geography logic

**Cons**:
- Vivec canals ARE connected to ocean (they're open to sea)
- Rivers flowing to ocean would be marked as ocean
- Requires pathfinding computation at load time

**Refinement**: Add "protection" zones around settlements that block flood-fill:
```cpp
bool isProtectedZone(const CellStore* cell) {
    // Cells within canton areas, river corridors, etc.
    return isVivecCanton(cell) || isRiverCorridor(cell);
}
```

#### Approach B: Distance-from-Shore Heuristic

**Concept**: Ocean = far from any land, Lake/River = near land on multiple sides.

```
Algorithm:
1. Calculate distance to nearest terrain for each water texel
2. If surrounded by terrain (distance < threshold in >2 directions): Lake
3. If distance > threshold in any direction toward map edge: Ocean
```

**Pros**:
- No connectivity computation
- Works for arbitrary coastlines
- Naturally creates transition zones

**Cons**:
- Bays and inlets might be misclassified
- Requires terrain heightfield sampling
- Tuning thresholds is tricky

#### Approach C: Hybrid Manual + Automatic

**Concept**: Use automatic detection with manual override database.

```
Priority order:
1. Check manual override database (lakes.json, WCOL records)
2. Check known settlement protection zones
3. Apply sea-level + perimeter heuristic
4. Default to Lake for ambiguous cases
```

**Data format** (extend existing lakes.json):
```json
{
  "waterBodies": [
    {
      "name": "Vivec Canals",
      "type": "lake",  // Force lake even though connected to ocean
      "cells": [[-3, -9], [-3, -10], ...],
      "color": [0.15, 0.25, 0.35]
    },
    {
      "name": "Odai River",
      "type": "river",
      "cells": [[-3, -3], [-3, -4], ...],
      "flowDirection": 180  // degrees, for future river flow effects
    }
  ]
}
```

**Pros**:
- Full control over edge cases
- Leverages existing WCOL system
- Works with mods that add water bodies

**Cons**:
- Requires manual data entry
- Mods need to provide water body definitions

### Recommendation: Approach C (Hybrid)

The hybrid approach is most practical because:
1. **Morrowind's geography is fixed** - we can enumerate special cases
2. **Vivec canals are the main edge case** - they're geographically connected to ocean but shouldn't show ocean waves
3. **The waterlevels branch already has the infrastructure** - just needs refinement
4. **Mods can extend via WCOL** - future-proof for new content

---

## Part 2: Shore Smoothing (Wave Damping Near Coast)

### The Challenge

FFT ocean produces uniform wave amplitude across the entire surface. Real oceans:
- Have large swells in open water
- Gradually reduce wave height approaching shore
- Show foam/breaking waves at coastline
- Have nearly calm water in protected bays

### Proposed Approaches

#### Approach A: Depth-Based Wave Attenuation

**Concept**: Sample terrain depth below water, reduce wave amplitude in shallow areas.

```glsl
// In ocean vertex shader
uniform sampler2D depthMap;  // Terrain depth below sea level

float waterDepth = texture(depthMap, worldPos.xy / worldScale).r;
float depthFactor = smoothstep(0.0, shallowThreshold, waterDepth);

// Attenuate displacement
vec3 displacement = sampleOceanDisplacement(uv);
displacement *= depthFactor;  // Reduce waves in shallow water
```

**Parameters**:
- `shallowThreshold`: Depth at which waves reach full amplitude (~50-100m)
- `minimumAmplitude`: Small ripples even in very shallow water (~10%)

**Pros**:
- Physically motivated (real wave shoaling)
- Smooth transitions
- Works with arbitrary coastlines

**Cons**:
- Requires depth texture (can reuse existing refraction depth)
- May need separate render pass for distant terrain depth
- Shore detection based purely on depth might miss some cases

#### Approach B: Ocean Mask Gradient

**Concept**: Instead of binary ocean mask, use gradient (0.0-1.0) for smooth transitions.

```cpp
// In WaterHeightField::generateOceanMask()
// Instead of 0/255, generate gradient based on:
// - Distance to nearest non-ocean cell
// - Distance to map edge
// - Manual softness overrides

float distanceToShore = computeDistanceField(oceanCells);
float maskValue = smoothstep(0.0, transitionDistance, distanceToShore);
```

**Shader usage**:
```glsl
uniform sampler2D oceanMask;  // 0.0 = shore, 1.0 = open ocean

float oceanness = texture(oceanMask, worldPos.xy / worldScale).r;

// Full waves in open ocean, reduced near shore
vec3 displacement = sampleOceanDisplacement(uv);
displacement *= mix(shoreAmplitude, 1.0, oceanness);
```

**Pros**:
- Single texture lookup
- Explicit control over transition zones
- Can encode protection zones into mask

**Cons**:
- Texture resolution limits transition smoothness
- Need to regenerate when cells load/unload

#### Approach C: Distance Field + Cascade Blending

**Concept**: Use different FFT cascade weights based on distance from shore.

The ocean already has 4 cascades:
- Cascade 0: Large swells (lowest frequency)
- Cascade 1-2: Medium waves
- Cascade 3: Small detail (highest frequency)

```glsl
// Near shore: suppress large swells, keep small ripples
float shoreDistance = texture(distanceField, worldPos.xy).r;

float cascade0Weight = smoothstep(0.0, 500.0, shoreDistance);   // Large waves only far out
float cascade1Weight = smoothstep(0.0, 200.0, shoreDistance);   // Medium waves fade
float cascade2Weight = smoothstep(0.0, 50.0, shoreDistance);    // Small waves closer
float cascade3Weight = 1.0;  // Tiny ripples everywhere

vec3 displacement =
    cascade0Weight * sampleCascade(0) +
    cascade1Weight * sampleCascade(1) +
    cascade2Weight * sampleCascade(2) +
    cascade3Weight * sampleCascade(3);
```

**Pros**:
- Realistic wave behavior (large waves don't reach shore)
- Maintains surface detail (small ripples) near shore
- Uses existing cascade system

**Cons**:
- More complex shader math
- Needs distance field texture

### Recommendation: Combine B + C

Use **ocean mask gradient** for coarse control and **cascade blending** for realistic wave behavior:

1. **Ocean Mask Gradient**: Controls overall "oceanness" (0 = protected water, 1 = open ocean)
2. **Depth-Based Cascade Weights**: Fine-tunes wave sizes based on water depth
3. **Shore Foam**: Add foam where depth is very shallow (future enhancement)

---

## Part 3: Visual Distance Transitions

### Challenge: Ocean Visible at All Distances

We want:
- **Far**: Visible wave motion on horizon (large swells)
- **Mid**: Detailed wave patterns with reflections
- **Near**: Full detail with foam, ripples

### Current Implementation

The ocean already has distance-based falloff for displacement amplitude:
```cpp
// ocean.vert
const float DISPLACEMENT_FALLOFF_START = 21759.0;  // ~300m
const float DISPLACEMENT_FALLOFF_END = 43518.0;    // ~600m

float distanceFalloff = 1.0 - smoothstep(
    DISPLACEMENT_FALLOFF_START,
    DISPLACEMENT_FALLOFF_END,
    length(position.xy - cameraPos.xy)
);
```

### Proposed Enhancement: LOD Cascade System

Extend the existing 4-cascade system for view-distance LOD:

```
Distance     | Cascades Active | Visual Result
-------------|-----------------|----------------------------------
0-100m       | 0,1,2,3         | Full detail, all wave sizes
100-500m     | 0,1,2           | Good detail, no micro-ripples
500-2000m    | 0,1             | Medium waves only
2000m+       | 0               | Large swells only (horizon motion)
```

This is already partially implemented via the normal sampling LOD:
```glsl
// ocean.frag
vec2 lodFactors = computeLODFactors(linearDepth);
```

**Enhancement needed**: Apply similar LOD to vertex displacement, not just normals.

---

## Part 4: Implementation Plan

### Phase 1: Ocean Masking (Leverage waterlevels branch)

1. **Merge WaterHeightField** from waterlevels to water branch
2. **Implement gradient mask generation**:
   - Add `generateOceanMaskGradient()` to WaterHeightField
   - Use distance transform from ocean cells
3. **Pass mask to ocean shader**:
   - Bind as texture uniform
   - Discard fragments where mask < threshold
4. **Add manual overrides**:
   - Vivec cantons → force lake
   - Rivers → force lake/river type

### Phase 2: Shore Wave Damping ✅ IMPLEMENTED

**Status: Complete** - Implemented using refraction depth for real-time shore detection.

1. **✅ Fragment shader shore detection**:
   - Samples refraction depth map early in fragment shader
   - Calculates `shoreDepthFactor` based on water depth
   - Uses `smoothstep()` for gradual transition

2. **✅ Cascade-based wave attenuation**:
   - Large wave cascades (0-1) fully suppressed near shore
   - Small ripple cascades (2-3) mostly preserved
   - Creates realistic wave behavior: big swells calm, small ripples remain

3. **✅ Shore foam boost**:
   - Extra foam added based on shore proximity
   - Simulates breaking wave effect

4. **✅ Lua console controls**:
   ```lua
   ocean.setShoreWaveAttenuation(0.8)  -- 0-1, wave reduction at shore
   ocean.setShoreDepthScale(500.0)     -- MW units, depth for full waves
   ocean.setShoreFoamBoost(1.5)        -- 0-5, extra foam at shore
   ```

**Files modified**:
- `files/shaders/compatibility/ocean.frag` - Shore smoothing logic
- `apps/openmw/mwrender/ocean.hpp/cpp` - Parameters and uniforms
- `apps/openmw/mwrender/water.hpp/cpp` - WaterManager interface
- `apps/openmw/mwlua/oceanbindings.cpp` - Lua bindings

### Phase 3: Cascade LOD Refinement

1. **Adjust cascade distance thresholds**:
   - Tune based on visual testing
   - Make configurable (debug/performance tradeoff)
2. **Add horizon motion**:
   - Ensure large swells visible at max distance
   - May need extended mesh coverage

### Phase 4: Polish

1. **Shore foam**:
   - ✅ Basic implementation via `shoreFoamBoost`
   - Future: Render foam sprites/geometry
2. **Breaking waves**:
   - Advanced: geometry shader for wave curl
   - Simple: foam texture where depth < wave height
3. **Sound integration**:
   - Wave intensity affects ocean ambience
   - Shore breaking sounds near coastline

---

## Technical Considerations

### Performance

| Feature | Cost | Mitigation |
|---------|------|------------|
| Ocean mask texture | 1 texture sample | Cache-friendly, low-res OK |
| Distance field | 1 texture sample | Combine with mask texture |
| Cascade blending | Already computed | Minor ALU overhead |
| Fragment discard | GPU occupancy hit | Only at boundaries, minimal |

### Memory

| Texture | Resolution | Format | Size |
|---------|------------|--------|------|
| Ocean mask gradient | 2048×2048 | R8 | 4 MB |
| Shore distance | 2048×2048 | R16F | 8 MB |
| (Combined) | 2048×2048 | RG16F | 8 MB |

**Recommendation**: Combine into single RG16F texture:
- R = Ocean mask (0-1 gradient)
- G = Shore distance (in world units / scale)

### Compatibility

- **Mods**: WCOL subrecord allows per-cell water type override
- **Settings**: All thresholds exposed via Lua console
- **Fallback**: If textures unavailable, use simple cell-based detection

---

## Open Questions

1. **How far should wave damping extend?**
   - 100m? 500m? Configurable?

2. **Should rivers have flow direction?**
   - Future feature: animated flow UVs
   - Useful for immersion but complex

3. **What about underwater view?**
   - Ocean waves from below look different
   - Currently not addressed

4. **Mod compatibility for new water bodies?**
   - WCOL works for height/color
   - Need WaterType field in ESM format?

---

## References

- [waterheightfield.hpp](../apps/openmw/mwrender/waterheightfield.hpp) - Existing classification system
- [OCEAN_MASKING_TODO.md](OCEAN_MASKING_TODO.md) - Previous implementation notes
- [lakes.json](../MultiLevelWater/lakes.json) - Manual lake definitions
- [ocean.frag](../files/shaders/compatibility/ocean.frag) - Current ocean shader
