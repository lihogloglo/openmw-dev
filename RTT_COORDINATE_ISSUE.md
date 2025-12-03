# RTT Deformation System - Coordinate Mapping Issue

## Problem Summary

The RTT (Render-to-Texture) camera system for terrain deformation has a **coordinate mapping issue**. The deformations appear offset from the character and the UV debug visualization shows axes are rotated ~45 degrees.

## Current Behavior (Debug Mode 1)

When debug mode 1 is enabled (`deformation debug mode = 1` in settings.cfg):
- **Expected**: Red increases going East (+X), Green increases going North (+Y)
- **Actual**: Red increases going South-East, Green increases going North-West

This 45-degree rotation persists regardless of camera orientation (top-down or bottom-up).

## System Architecture

### Components

1. **Depth Camera** (`snowdeformation.cpp`)
   - Renders actors to `mObjectMaskMap` texture
   - Currently configured as bottom-up (eye below ground, looking +Z)
   - Uses orthographic projection centered on player

2. **Snow Simulation** (`snowsimulation.cpp`)
   - Update pass: Combines object mask with previous frame, applies scrolling offset
   - Blur passes: Smooths the deformation
   - Copy pass: Copies result for next frame

3. **Terrain Shader** (`terrain.vert`, `terrain.frag`)
   - Samples deformation map using UV calculated from world position:
     ```glsl
     vec2 deformUV = (worldPos.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
     ```

### Coordinate Flow

```
World Position (X, Y, Z)
    ↓
Depth Camera (renders to Object Mask)
    ↓
Snow Update Shader (combines with previous frame)
    ↓
Blur Passes
    ↓
Terrain Shader (samples using world-to-UV transform)
```

## Key Files

- `components/terrain/snowdeformation.cpp` - Depth camera setup, RTT initialization
- `components/terrain/snowsimulation.cpp` - Ping-pong buffers, update/blur passes
- `files/shaders/compatibility/snow_update.frag` - Accumulation shader
- `files/shaders/compatibility/terrain.vert` - Terrain vertex displacement
- `files/shaders/compatibility/terrain.frag` - Debug visualization, normal perturbation

## Debug System Added

A debug visualization system was added to help diagnose the issue:

### Settings
```ini
[Terrain]
deformation debug mode = 1
```

### Debug Modes
- `0` = Off (normal rendering)
- `1` = Show UV coordinates: R=U (should be East), G=V (should be North), Blue=out-of-bounds
- `2` = Show deformation value as grayscale (white=object, black=none)
- `3` = Show world offset from RTT origin

## What Was Tried

### 1. Bottom-Up Camera with up=(0,1,0)
```cpp
osg::Vec3f eye = mRTTCenter - osg::Vec3f(0, 0, farPlane);
osg::Vec3f center = mRTTCenter;
osg::Vec3f upVec = osg::Vec3f(0, 1, 0);
```
**Result**: Axes rotated 45 degrees (Red=SE, Green=NW)

### 2. Bottom-Up Camera with up=(0,-1,0) and flipped ortho Y
```cpp
osg::Vec3f upVec = osg::Vec3f(0, -1, 0);
mDepthCamera->setProjectionMatrixAsOrtho(-halfSize, halfSize, halfSize, -halfSize, near, far);
```
**Result**: Same 45-degree rotation

### 3. Top-Down Camera with up=(0,1,0)
```cpp
osg::Vec3f eye = mRTTCenter + osg::Vec3f(0, 0, farPlane);
osg::Vec3f upVec = osg::Vec3f(0, 1, 0);
```
**Result**: Same 45-degree rotation

## Key Insight

The fact that **both top-down and bottom-up cameras show the same 45-degree rotation** suggests the issue is NOT in the camera orientation, but somewhere else in the pipeline:

1. **Possible issue in terrain shader's world position** (`passWorldPos`)
   - The `chunkWorldOffset` uniform might not be correct
   - The vertex position might already be in a rotated space

2. **Possible issue in how world position is passed**
   - Check `terrain.vert` line: `passWorldPos = vertex.xyz + chunkWorldOffset;`

3. **Possible OpenMW-specific coordinate system**
   - OpenMW might use a different convention than expected
   - The "world" coordinates in shaders might already be transformed

## Next Steps to Investigate

1. **Verify `passWorldPos` is correct**:
   - Add debug mode to output `passWorldPos.xy` directly as colors
   - Compare with actual world coordinates from game

2. **Check `chunkWorldOffset` uniform**:
   - Log the values being passed
   - Verify they match expected chunk positions

3. **Test with a simple quad at known world position**:
   - Place a debug object at (1000, 0, 0) and verify UV shows more red
   - Place at (0, 1000, 0) and verify UV shows more green

4. **Check if OpenMW uses rotated world coordinates**:
   - Some engines rotate world axes (e.g., Y-up vs Z-up conversions)
   - Check if there's a 45-degree rotation in the base coordinate system

## OpenMW Coordinate System

- **Z is up** (confirmed)
- **22.1 units = 1 foot** (confirmed)
- X and Y form the ground plane
- Need to verify: Which direction is North? Which is East?

## Files Modified During Debug Session

1. `components/settings/categories/terrain.hpp` - Added `mDeformationDebugMode`
2. `files/settings-default.cfg` - Added `deformation debug mode` setting
3. `components/terrain/snowdeformationupdater.cpp` - Pass debug mode to shader
4. `files/shaders/compatibility/terrain.frag` - Added debug visualization
5. `files/shaders/core/terrain.frag` - Added debug visualization
6. `components/terrain/snowdeformation.cpp` - Various camera orientation attempts
7. `files/shaders/compatibility/snow_update.frag` - Changed offset from `+` to `-`

## Current State

- Bottom-up camera is in place (for flying creature filtering)
- Debug visualization is working (mode 1, 2, 3)
- Coordinate mapping issue remains unsolved
- The issue appears to be upstream of the camera (in world coordinate handling)
