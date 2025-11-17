# Terrain Weights Diagnostic Report

**Date:** 2025-11-17
**Update:** 2025-11-17 - FIXED with grid-snapping boundary vertex sharing
**Status:** ✅ RESOLVED

---

## Original Issue (FIXED)

**Seams at chunk boundaries** - Adjacent chunks computed different weights for boundary vertices.

---

## Solution Implemented: Grid-Snapping Boundary Vertex Sharing

**Date:** 2025-11-17
**Location:** `components/terrain/terrainweights.cpp:105-162`

### How It Works

1. **Convert to Land Texture Grid:** Vertex positions are converted to Morrowind's land texture grid (16x16 texels per cell)

2. **Snap to Nearest Texel:** Positions are rounded to the nearest land texel using `std::round()`
   - **This is the key:** Boundary vertices in adjacent chunks round to the SAME texel!

3. **Convert to Chunk-Local UV:** The snapped texel position is converted back to UV coordinates within each chunk's blendmap

4. **Sample Consistently:** Both chunks sample from the same logical position in the land data

### Why This Eliminates Seams

```
Before (seams):
Chunk A boundary vertex: world pos 512.3 → samples Chunk A's blendmap at u=1.0
Chunk B boundary vertex: world pos 512.3 → samples Chunk B's blendmap at u=0.0
Result: Different samples, SEAM!

After (seamless):
Chunk A boundary vertex: world pos 512.3 → land texel 1.0 → rounds to 1 → samples texel 1
Chunk B boundary vertex: world pos 512.3 → land texel 1.0 → rounds to 1 → samples texel 1
Result: Same sample, NO SEAM!
```

### Performance Impact

**Minimal** - The grid-snapping adds:
- 2 multiplications (convert to land grid)
- 2 rounds (snap to nearest)
- 2 subtractions, 1 division (convert back to UV)

All very fast integer/float operations, negligible overhead.

---

## Bilinear Interpolation Removed

**Date:** 2025-11-17

### Rationale

1. **Unnecessary complexity** - Grid-snapping already ensures boundary consistency
2. **Marginal benefit** - Morrowind blendmaps are binary (0/255), so interpolation only creates 1-2 pixel transitions
3. **Simpler is better** - Nearest-neighbor sampling is faster and easier to debug

### What Was Removed

- Bilinear interpolation code in `sampleBlendmap()` (was 55 lines, now 30 lines)
- 4-corner sampling and weight calculation
- Float-based UV arithmetic for sub-pixel positioning

### What Replaced It

**Nearest-neighbor sampling:**
```cpp
int x = static_cast<int>(std::round(u * (blendmap->s() - 1)));
int y = static_cast<int>(std::round(v * (blendmap->t() - 1)));
float blendValue = pixel[y * width + x] / 255.0f;
```

Simple, fast, effective.

---

## Original Root Cause Analysis (Historical)

### Problem 1: Binary Weights (No Gradients) - EXPECTED BEHAVIOR

**How weight computation works currently:**
```
For each chunk:
1. Get blendmaps for THIS chunk: getBlendmaps(chunkSize, chunkCenter, ...)
2. For each vertex in chunk:
   a. vertexPos is chunk-local coordinates (relative to chunk center)
   b. Convert to UV: u = (vertexPos.x + halfSize) / (chunkSize * cellWorldSize)
   c. Sample blendmap at UV [0,1]
   d. Compute weight based on blendmap value
```

**The Problem:**
```
Chunk A (center=0.0):                Chunk B (center=1.0):
├─────────┼─────────┤                ├─────────┼─────────┤
│         │ Vertex  │                │ Vertex  │         │
│         │ at +512 │ <-- SAME -->   │ at -512 │         │
│         │         │   WORLD POS    │         │         │
└─────────┴─────────┘                └─────────┴─────────┘

Chunk A: vertex at +512 (local) → UV = 1.0 → samples RIGHT edge of Chunk A's blendmap
Chunk B: vertex at -512 (local) → UV = 0.0 → samples LEFT edge of Chunk B's blendmap

Result: Same world position, DIFFERENT blendmap samples = SEAM!
```

**Why this causes seams:**
1. Each chunk gets its own set of blendmaps
2. Blendmaps might not align perfectly at chunk boundaries
3. UV coordinates are chunk-local, not world-space
4. Boundary vertices in adjacent chunks sample different blendmap pixels

---

## Proposed Solutions

### Solution 1: World-Space UV Coordinates (BEST)

**Concept:** Use world-space coordinates for blendmap sampling, not chunk-local.

**Implementation:**
```cpp
// Instead of chunk-local UV:
float u = (vertexPos.x() + halfSize) / (chunkSize * cellWorldSize);  // CURRENT (WRONG)

// Use world-space UV:
osg::Vec3f worldPos = vertexPos + chunkWorldOffset;  // Convert to world space
float worldU = worldPos.x() / cellWorldSize;  // World-space UV
float worldV = worldPos.y() / cellWorldSize;
// Then sample blendmap at world-space coordinates
```

**Requirements:**
- Need to know: Do blendmaps cover a cell, a region, or individual chunks?
- Need to understand OpenMW's terrain storage coordinate system
- May need to fetch blendmaps differently (global, not per-chunk)

---

### Solution 2: Explicit Boundary Smoothing

**Concept:** Detect boundary vertices and average weights from adjacent chunks.

**Implementation:**
```cpp
if (isChunkBoundaryVertex(vertexPos))
{
    // Sample from both adjacent chunks
    weight = (sampleFromChunkA + sampleFromChunkB) * 0.5f;
}
```

**Pros:**
- Guarantees smooth boundaries
- Doesn't require understanding global coordinate system

**Cons:**
- Complex to implement (need to identify boundaries, fetch neighbor chunks)
- Performance overhead (double sampling)
- Doesn't fix the underlying cause

---

### Solution 3: Bilinear Blendmap Filtering

**Concept:** Use bilinear interpolation when sampling blendmaps instead of nearest-neighbor.

**Current:**
```cpp
int x = static_cast<int>(u * (blendmap->s() - 1));  // Nearest neighbor
int y = static_cast<int>(v * (blendmap->t() - 1));
float value = pixel[x][y];
```

**Proposed:**
```cpp
// Bilinear interpolation
float fx = u * (blendmap->s() - 1);
float fy = v * (blendmap->t() - 1);
int x0 = floor(fx), x1 = x0 + 1;
int y0 = floor(fy), y1 = y0 + 1;
float wx = fx - x0, wy = fy - y0;

float v00 = getPixel(x0, y0);
float v01 = getPixel(x0, y1);
float v10 = getPixel(x1, y0);
float v11 = getPixel(x1, y1);

float value = lerp(lerp(v00, v10, wx), lerp(v01, v11, wx), wy);
```

**Pros:**
- Creates smooth gradients even from binary blendmaps
- Simple to implement
- May reduce seams

**Cons:**
- Doesn't fix chunk boundary alignment issue
- Only helps if blendmaps have SOME variation

---

## Immediate Next Steps

1. **Enable verbose logging** - Check what blendmap values are being sampled
2. **Enable debug visualization** - Visually confirm binary vs blended weights
3. **Examine chunk boundary vertices** - Log weights for vertices at chunk edges
4. **Test bilinear filtering** - Quick fix that might help

---

## Questions to Answer

1. **Do Morrowind blendmaps have gradients?** Or are they binary masks?
2. **What coordinate system do blendmaps use?** Cell-space? World-space? Chunk-space?
3. **Do adjacent chunks share the same blendmap?** Or get separate blendmaps?
4. **How big are blendmaps?** Resolution? Coverage area?

Once we answer these, we'll know which solution to implement.
