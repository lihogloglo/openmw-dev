# RTT Deformation System - Coordinate Mapping Issue

## Problem Summary

The RTT (Render-to-Texture) camera system for terrain deformation has a **coordinate mapping issue**. Deformations appear **in the wrong location** relative to where the character walks.

## Current Symptom (Latest Testing)

**Footprints appear in the WRONG DIRECTION from where the character walked.**
- When walking in any direction, the deformation appears reversed/offset
- This happens regardless of camera orientation (top-down or bottom-up)

## Working Reference Commit

**Commit `19eeb462d7f563375c7de265219dc0da550799b0`** had working deformation (though camera was "upside down").

Key settings in that commit:
```cpp
// snowdeformation.cpp - TOP-DOWN camera
osg::Vec3f eye = mRTTCenter + osg::Vec3f(0, 0, farPlane);  // ABOVE player
osg::Vec3f center = mRTTCenter;
osg::Vec3f up = osg::Vec3f(0, 1, 0);
```

```glsl
// snow_update.frag - PLUS offset
vec2 oldUV = uv + offset;
```

## Current Configuration

```cpp
// snowdeformation.cpp - BOTTOM-UP camera (for flying creature filtering)
osg::Vec3f eye = mRTTCenter - osg::Vec3f(0, 0, farPlane);  // BELOW player
osg::Vec3f center = mRTTCenter;
osg::Vec3f upVec = osg::Vec3f(0, 1, 0);

// Standard ortho projection
mDepthCamera->setProjectionMatrixAsOrtho(
    -halfSize, halfSize,   // left, right (X)
    -halfSize, halfSize,   // bottom, top (Y)
    nearPlane, farPlane);
```

```glsl
// snow_update.frag - currently PLUS offset (reverted)
vec2 oldUV = uv + offset;
```

## System Architecture

### Pipeline Flow
```
1. Depth Camera renders actors to Object Mask texture
   - Bottom-up orthographic view centered on player
   - Outputs white (1.0) where actors exist

2. Snow Update Shader (snow_update.frag)
   - Reads previous frame with scrolling offset
   - Combines with current object mask
   - Writes to accumulation buffer

3. Blur Passes (horizontal then vertical)
   - Smooths the deformation edges

4. Terrain Shader samples result
   - Calculates UV: deformUV = (worldPos.xy - origin.xy) / scale + 0.5
   - Displaces vertices based on deformation value
```

### Key Files
| File | Purpose |
|------|---------|
| `components/terrain/snowdeformation.cpp` | Depth camera setup, RTT initialization |
| `components/terrain/snowsimulation.cpp` | Ping-pong buffers, update/blur passes |
| `files/shaders/compatibility/snow_update.frag` | Accumulation with scrolling |
| `files/shaders/compatibility/terrain.vert` | UV calculation, vertex displacement |
| `files/shaders/compatibility/terrain.frag` | Debug visualization, normal perturbation |
| `components/settings/categories/terrain.hpp` | Debug mode setting (0-11 range) |

## Debug System

### Settings
```ini
[Terrain]
deformation debug mode = 2
```

**IMPORTANT**: You must rebuild after changing the setting - it's read at initialization.

### Debug Modes
| Mode | What it shows |
|------|---------------|
| 0 | Off (normal rendering) |
| 1 | UV coordinates: R=U, G=V (orange at player) |
| 2 | Deformation map value (white=footprint, black=none) |
| 3 | World position relative to origin |
| 4 | Cardinal directions: RED=East, GREEN=North, CYAN=West, MAGENTA=South |
| 5 | Deformation map + yellow crosshair at UV center |
| 6 | Raw texture sweep (bypasses UV calculation) |
| 7 | Full RGBA of deformation map |
| 8 | Raw passWorldPos.xy as colors |
| 9 | snowRTTWorldOrigin.xy (player pos) as colors |
| 10 | Gradient: R=X position, G=Y position |
| 11 | chunkWorldOffset uniform as colors |

## Camera Flip Analysis

When switching from top-down to bottom-up camera:

**Top-down (looking -Z with up=+Y):**
- Right vector = cross(-Z, +Y) = **+X**
- World +X → Texture +U (right side)

**Bottom-up (looking +Z with up=+Y):**
- Right vector = cross(+Z, +Y) = **-X**
- World +X → Texture **-U** (left side, FLIPPED!)

This means the X axis is mirrored when using bottom-up camera with standard projection.

## Attempted Fixes (None Worked)

### 1. Flip ortho projection X
```cpp
mDepthCamera->setProjectionMatrixAsOrtho(
    halfSize, -halfSize,   // left, right SWAPPED
    -halfSize, halfSize,
    nearPlane, farPlane);
```
**Result**: Still wrong

### 2. Change offset direction in snow_update.frag
```glsl
vec2 oldUV = uv - offset;  // Changed from + to -
```
**Result**: Still wrong (footprints in front instead of behind)

### 3. Revert offset to original
```glsl
vec2 oldUV = uv + offset;  // Back to original
```
**Result**: Still wrong

## OpenMW Coordinate System

- **Z is up** (confirmed)
- **+Y is North** (confirmed from localmap.cpp: `return osg::Vec2f(0, 1)` for default north)
- **+X is East** (implied)
- **22.1 units ≈ 1 foot**

## Hypotheses to Test

### 1. The UV calculation in terrain shader doesn't match camera rendering
The terrain shader calculates:
```glsl
vec2 deformUV = (worldPos.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
```

If the camera renders with X flipped, we might need:
```glsl
vec2 deformUV;
deformUV.x = 0.5 - (worldPos.x - snowRTTWorldOrigin.x) / snowRTTScale;  // FLIP X
deformUV.y = (worldPos.y - snowRTTWorldOrigin.y) / snowRTTScale + 0.5;
```

### 2. The simulation offset needs both X and Y flipped for bottom-up
With bottom-up camera, both the rendering AND the scrolling coordinate systems change.

### 3. Something else is transforming coordinates
There might be a model matrix or other transform applied to terrain chunks that we're not accounting for.

## Next Steps

1. **Use debug mode 6** - This bypasses UV calculation entirely and shows raw texture. Walk around and see where footprints appear in the raw texture.

2. **Compare object mask dump** - The code dumps `object_mask_dump.png` every 600 frames. Check if the actor appears in the correct position in that texture.

3. **Try flipping UV.x in terrain shader** - Add a test where we sample at `(1.0 - deformUV.x, deformUV.y)` to see if that fixes alignment.

4. **Log actual values** - Add C++ logging to print `mRTTCenter`, `playerPos`, and the offset being sent to the shader.

## Files Modified

1. `components/settings/categories/terrain.hpp` - Debug mode range extended to 0-11
2. `files/shaders/compatibility/terrain.frag` - Extended debug modes (1-11)
3. `files/shaders/core/terrain.frag` - Extended debug modes
4. `components/terrain/snowdeformation.cpp` - Camera configuration
5. `files/shaders/compatibility/snow_update.frag` - Offset direction (currently `+`)

## Current State

- Bottom-up camera is configured (for flying creature filtering)
- Debug modes 1-11 available (rebuild required after changing setting)
- **Coordinate mapping issue remains UNSOLVED**
- Footprints appear in wrong location relative to character movement
