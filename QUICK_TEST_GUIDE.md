# Snow Deformation - Quick Test Guide

## 🚀 Quick Start (2 Minutes)

### 1. Build (30 seconds)
```bash
cd build
cmake --build . --target openmw
```

### 2. Run (10 seconds)
```bash
./openmw.exe
```

### 3. Test (1 minute)
1. Load any save game
2. Walk forward 10-20 steps
3. Turn around and look at the ground

## ✅ What You Should See

**BRIGHT RED TERRAIN** behind you where you walked!

The red overlay is a debug feature that makes deformation VERY obvious.

## ❌ Troubleshooting

### Problem: No red terrain

**Check logs for these messages (in order):**

1. **Initialization:**
   ```
   SnowDeformation: RTT cameras created
   ```
   ❌ Missing → Camera setup failed, check error logs

2. **Movement:**
   ```
   SnowDeformation: Active footprints=X
   SnowDeformation: Added footprint at (X,Y)
   ```
   ❌ Missing → Walk more! Need to move 15+ units

3. **Rendering:**
   ```
   SnowDeformation: renderFootprintsToTexture - rendering X footprints
   Terrain material: Snow deformation ENABLED for chunk
   ```
   ❌ Missing → Shader integration broken

4. **Visual Result:**
   Turn around → See red terrain?
   ❌ NO → Coordinate transform or sampling issue

### Quick Fixes

**If no footprints created:**
- Lower interval in `snowdeformation.hpp`:
  ```cpp
  static constexpr float DEFAULT_FOOTPRINT_INTERVAL = 1.0f;  // Was 15.0
  ```

**If footprints created but no red:**
- Check that terrain shader is compiling (look for shader errors in log)
- Verify texture is valid (log should say "texture=valid")

**To make EVEN MORE visible:**
- In `terrain.frag`, change this line:
  ```glsl
  gl_FragData[0].xyz = mix(gl_FragData[0].xyz, vec3(1.0, 0.0, 0.0), debugDeformation * 5.0);
  ```
  To:
  ```glsl
  gl_FragData[0].xyz = vec3(1.0, 0.0, 0.0);  // Pure red where deformed
  ```

## 📊 Current Debug Settings

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Footprint Radius | 80 units | VERY large (10x normal) |
| Deformation Strength | 5.0 | VERY deep (10x normal) |
| Footprint Intensity | 1.0 | Maximum |
| Debug Overlay | RED | Shows any deformation > 0.001 |

**These are intentionally EXTREME values for debugging!**

Once it works, reduce them for realistic snow trails.

## 🎯 Success Looks Like

```
                  You
                   🚶
                   ↓
    [Normal Terrain]
            ↓
    [RED TERRAIN] ← You walked here
            ↓
    [RED TERRAIN] ← And here
            ↓
    [RED TERRAIN] ← And here
```

Red areas should:
- ✅ Appear where you walked
- ✅ Follow you as you move
- ✅ Fade slowly over time (decay)
- ✅ Be VERY bright and obvious

## 📝 Log File Location

Logs are usually in:
- Windows: `Documents/My Games/OpenMW/openmw.log`
- Or console output if run from terminal

## 🐛 Advanced Debugging

If you still see nothing, see detailed guides:
- [DEBUGGING_SNOW_DEFORMATION.md](DEBUGGING_SNOW_DEFORMATION.md) - Complete walkthrough
- [FIXES_APPLIED_SUMMARY.md](FIXES_APPLIED_SUMMARY.md) - What was changed

## 💡 Pro Tip

Add this to terrain.frag for MAXIMUM debug visibility:

```glsl
#if @snowDeformation
    // EXTREME DEBUG: Show terrain is using deformation shader
    gl_FragData[0].xyz += vec3(0.1, 0.0, 0.0);  // Slight red tint on ALL terrain

    // Show actual deformation as BRIGHT red
    if (debugDeformation > 0.001) {
        gl_FragData[0].xyz = vec3(1.0, 0.0, 0.0);  // Pure red
    }
#endif
```

This will:
1. Make ALL terrain slightly reddish (proves shader is running)
2. Make deformed terrain BRIGHT red (proves deformation is working)

---

**If you see red terrain, IT WORKS! 🎉**

Next step: Remove debug overlay and tune to realistic values.
