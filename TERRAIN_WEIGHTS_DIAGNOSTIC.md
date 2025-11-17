# Terrain Weights Diagnostic Report

**Date:** 2025-11-17
**Issue:** Seams at chunk boundaries, no blended terrain weights

---

## User Observations

1. **Never see blended terrain** - All vertices have discrete heights:
   - 0 units = rock (0,0,0,1)
   - 10 units = mud (0,0,1,0)
   - 20 units = ash (0,1,0,0)
   - 50 units = snow (1,0,0,0)
   - **Never intermediate values** like 25 units (50% snow + 50% rock)

2. **Seams ALWAYS at chunk boundaries** - Never within a chunk
   - This confirms the problem is chunk-related, not texture-related

3. **Question:** Is blending information accessible?

---

## Root Cause Analysis

### Problem 1: Binary Weights (No Gradients)

**Hypothesis:** Blendmaps contain binary values (0 or 1) instead of gradients (0.0-1.0).

**Evidence:**
- Discrete heights only
- No intermediate weight values observed

**Why this happens:**
- Morrowind's terrain blendmaps might be low-resolution masks
- Blendmap pixels might be binary (fully on/off)
- No smooth transitions between texture regions

**Test:** Check logs for:
```
[TERRAIN WEIGHTS] Blendmap sample = <value>
[TERRAIN WEIGHTS] FOUND BLENDED WEIGHT!  <- Will log if any non-binary weight found
```

---

### Problem 2: Chunk Boundary Misalignment

**Hypothesis:** Adjacent chunks compute different weights for the same world position.

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
