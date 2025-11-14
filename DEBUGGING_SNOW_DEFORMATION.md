# Debugging Snow Deformation - Comprehensive Guide

## Problem Summary

The snow deformation system is logging footprints but nothing is visible on screen. This could be due to several issues:

1. **RTT cameras not rendering** - The render-to-texture might not be executing
2. **Shader not applying** - The @snowDeformation define might not be set correctly
3. **Deformation values too small** - The effect might be too subtle
4. **Texture not binding** - The deformation texture might not be accessible to terrain shaders
5. **Coordinate system mismatch** - World positions might not map correctly to texture UVs

## Debug Features Added

### 1. Visual Debug Overlay in Terrain Shader

**What it does:** Makes any deformed terrain appear BRIGHT RED

**Files modified:**
- [terrain.vert](files/shaders/compatibility/terrain.vert) - Added `debugDeformation` varying
- [terrain.frag](files/shaders/compatibility/terrain.frag) - Added red color overlay for deformed areas

**How it works:**
```glsl
// In vertex shader - sample and pass deformation value
float deformation = sampleDeformation(worldPosXY);
debugDeformation = deformation;

// In fragment shader - show deformation as red
if (debugDeformation > 0.001) {
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, vec3(1.0, 0.0, 0.0), debugDeformation * 5.0);
    gl_FragData[0].xyz += vec3(debugDeformation * 2.0, 0.0, 0.0);
}
```

**Expected result:** When you walk on snow, the terrain should turn RED wherever there's deformation.

### 2. Enhanced Logging

**New log messages added:**

#### In snowdeformation.cpp:
```
"SnowDeformation: renderFootprintsToTexture - rendering X footprints to RTT camera"
"SnowDeformation: Added X footprint quads to RTT camera"
```

#### In material.cpp:
```
"Terrain material: Snow deformation ENABLED for chunk"
```

**How to enable:** Logs are at Debug::Info level and should appear in the console/log file.

### 3. Debug Helper Classes

**Files created:**
- [snowdeformationdebug.hpp](apps/openmw/mwrender/snowdeformationdebug.hpp)
- [snowdeformationdebug.cpp](apps/openmw/mwrender/snowdeformationdebug.cpp)

**Features (not yet integrated):**
- HUD texture overlay - Shows deformation texture in corner of screen
- Footprint markers - Visual spheres at footprint positions
- Wireframe visualization - Shows deformation mesh
- Bounds visualization - Shows deformation area coverage

## Testing Steps

### Step 1: Build with Debug Features

```bash
cd build
cmake --build . --target openmw
```

### Step 2: Run and Check Logs

Look for these critical messages in order:

1. **Initialization:**
   ```
   SnowDeformation: RTT cameras created (decay order=0, footprints order=1)
   SnowDeformation (material.cpp): First setSnowDeformationData call - enabled=true, texture=valid
   ```

2. **Movement:**
   ```
   SnowDeformation: Active footprints=X PlayerPos=(x,y,z)
   SnowDeformation: Added footprint at (x,y) intensity=0.8
   ```

3. **Rendering:**
   ```
   SnowDeformation: updateDeformationTexture - X footprints
   SnowDeformation: renderFootprintsToTexture - rendering X footprints to RTT camera
   Terrain material: Snow deformation ENABLED for chunk
   ```

### Step 3: Visual Check

**What you should see:**
- When you walk forward, the terrain around your character should turn **BRIGHT RED**
- The red areas should follow you as you move
- Red intensity should fade over time (as footprints decay)

**If you DON'T see red terrain:**
- The shader isn't applying deformation
- Continue to Step 4 for diagnosis

### Step 4: Diagnose the Issue

#### Scenario A: No "Snow deformation ENABLED" messages

**Problem:** The @snowDeformation define is not being set

**Check:**
1. Is `sSnowDeformationData.enabled` true?
2. Is `sSnowDeformationData.texture` not null?

**Fix in [renderingmanager.cpp](apps/openmw/mwrender/renderingmanager.cpp):**
```cpp
// Verify this is being called every frame:
Terrain::setSnowDeformationData(
    mSnowDeformation->isEnabled(),
    mSnowDeformation->getDeformationTexture(),
    mSnowDeformation->getDeformationStrength(),
    mSnowDeformation->getTextureCenter(),
    mSnowDeformation->getWorldTextureSize()
);
```

#### Scenario B: "ENABLED" messages but no footprints

**Problem:** Player isn't moving or footprints aren't being created

**Check:** Is `mLastFootprintDist` reaching `mFootprintInterval` threshold?

**Current settings:**
- `DEFAULT_FOOTPRINT_INTERVAL = 15.0f` (15 world units ≈ 3 meters)
- You need to walk at least 15 units to create a footprint

**Temporary fix:** Lower the interval in [snowdeformation.hpp](apps/openmw/mwrender/snowdeformation.hpp):
```cpp
static constexpr float DEFAULT_FOOTPRINT_INTERVAL = 1.0f;  // Create footprints more frequently
```

#### Scenario C: Footprints created but terrain not red

**Problem:** Shader sampling is returning 0.0

**Possible causes:**

1. **Texture coordinates wrong:**
   - Check `mTextureCenter` vs `playerPos`
   - Check `worldTextureSize` (should be 300.0)

2. **RTT cameras not rendering:**
   - Check camera node masks
   - Verify render order (decay=0, footprints=1)

3. **Footprint quads outside texture bounds:**
   - Check `createFootprintQuad()` UV calculations
   - Ensure quads are in [0,1] range

4. **Deformation texture not bound to unit 4:**
   - Check in terrain material.cpp that texture unit 4 is used
   - Verify shader samples from uniform `deformationMap`

## Critical Values to Check

### World Units vs Texture Size

The deformation texture covers a **300×300 world unit** area:

```cpp
DEFAULT_WORLD_TEXTURE_SIZE = 300.0f;  // World units
DEFORMATION_TEXTURE_SIZE = 1024;       // Pixels
```

**Resolution:** ~3.4 pixels per world unit

### Footprint Parameters

Current footprint settings (in snowdeformation.cpp):

```cpp
footprint.intensity = 0.8f;   // Should be very visible
footprint.radius = 24.0f;     // 24 world units ≈ 7 pixels in texture
```

**WARNING:** With radius=24 and worldTextureSize=300, each footprint is only about 7 pixels wide in the 1024px texture!

**This might be the issue - footprints are TOO SMALL.**

### Recommended Fix: Increase Footprint Size

In [snowdeformation.cpp](apps/openmw/mwrender/snowdeformation.cpp), line ~244:

```cpp
// OLD (possibly too small):
footprint.radius = 24.0f;

// NEW (more visible):
footprint.radius = 50.0f;  // ~170 pixels in texture, very visible
```

## Quick Fixes to Try

### Fix 1: Make Footprints Much Larger

```cpp
// In snowdeformation.cpp, update() method:
footprint.radius = 80.0f;     // Huge footprints for testing
footprint.intensity = 1.0f;    // Max intensity
```

### Fix 2: Lower Footprint Interval

```cpp
// In snowdeformation.hpp:
static constexpr float DEFAULT_FOOTPRINT_INTERVAL = 1.0f;  // Create footprints every 1 unit
```

### Fix 3: Increase Deformation Strength

```cpp
// In snowdeformation.cpp constructor:
mDeformationStrength(2.0f)  // Double the displacement
```

### Fix 4: Force Debug Output

Add this to terrain.frag to ALWAYS show red (even without deformation):

```glsl
#if @snowDeformation
    // EXTREME DEBUG: Always show red to verify shader is running
    gl_FragData[0].xyz += vec3(0.2, 0.0, 0.0);  // Constant red tint
#endif
```

If terrain is red everywhere, shader is running but deformation values are 0.

## Advanced Debugging

### Dump Deformation Texture to File

Add this method to SnowDeformationManager:

```cpp
void dumpTextureToFile()
{
    osg::ref_ptr<osg::Image> image = new osg::Image;
    image->allocateImage(1024, 1024, 1, GL_RED, GL_FLOAT);

    // Read texture from GPU
    mDeformationTexture->getImage()->readImageFromCurrentTexture(0, true);

    // Save as file
    osgDB::writeImageFile(*image, "deformation_debug.png");
    Log(Debug::Info) << "Saved deformation texture to deformation_debug.png";
}
```

Call this from update() every few seconds to see if the texture actually contains data.

### Check Camera Rendering

Add to snowdeformation.cpp createDeformationCamera():

```cpp
// After camera setup:
mDeformationCamera->setRenderingCache(nullptr);  // Force re-render every frame
mDecayCamera->setRenderingCache(nullptr);
```

### Verify Texture Binding

In terrain.vert, add debug output:

```glsl
#if @snowDeformation
    float testSample = texture2D(deformationMap, vec2(0.5, 0.5)).r;
    if (testSample > 0.001) {
        // Texture has data! Output to log somehow
    }
#endif
```

## Expected Performance

With debug visualization enabled:
- **FPS impact:** Minimal (~1-2 FPS loss from extra shader math)
- **Log spam:** ~3-4 messages per second at Debug::Info level
- **Visual:** Bright red terrain wherever player has walked

## Next Steps After Debugging

Once you can see red terrain:

1. **Remove debug visualization** - Comment out the red overlay in terrain.frag
2. **Verify displacement** - Check if vertices are actually moving down
3. **Tune parameters** - Adjust radius, strength, decay rates
4. **Add material detection** - Make it work on different terrain types
5. **Optimize** - Reduce logging, improve performance

## Common Issues and Solutions

| Issue | Symptom | Solution |
|-------|---------|----------|
| No red overlay anywhere | Shader not compiling or @snowDeformation not set | Check logs for shader errors, verify material.cpp sets define |
| Red overlay everywhere | Deformation always > 0 or debug code wrong | Check sampleDeformation() returns 0 outside texture bounds |
| Footprints logged but no red | RTT not rendering or texture empty | Check camera node masks, verify quads are added |
| Red flickers | Texture ping-pong failing | Check swap logic in updateDeformationTexture() |
| Red in wrong location | Coordinate transform wrong | Check worldPosXY calculation, verify textureCenter |

## Files Modified Summary

### Shaders:
- ✅ [terrain.vert](files/shaders/compatibility/terrain.vert) - Added debug visualization
- ✅ [terrain.frag](files/shaders/compatibility/terrain.frag) - Added red overlay

### C++ Core:
- ✅ [snowdeformation.cpp](apps/openmw/mwrender/snowdeformation.cpp) - Added logging

### Terrain Integration:
- ✅ [material.cpp](components/terrain/material.cpp) - Added logging

### Debug Utilities:
- ✅ [snowdeformationdebug.hpp](apps/openmw/mwrender/snowdeformationdebug.hpp) - Debug class
- ✅ [snowdeformationdebug.cpp](apps/openmw/mwrender/snowdeformationdebug.cpp) - Debug implementation

### Helper Scripts:
- ✅ [test_snow_deformation.bat](test_snow_deformation.bat) - Test runner

## Contact & Further Help

If the terrain turns red when you walk, **the system is working!**

If not, check:
1. Shader compilation errors in logs
2. Texture creation failures
3. Camera rendering issues (node masks)

The red overlay is a VERY aggressive debug tool - if the deformation system is sampling ANY non-zero values from the texture, you'll see bright red terrain.

---

**Good luck! The snow trails are so close!** ❄️👣
