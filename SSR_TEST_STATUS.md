# SSR Testing on Old Water Plane

## Configuration

### What's Enabled
✅ **Old Water Plane** - The legacy flat quad water
✅ **SSR System** - Screen-space reflections
✅ **Cubemap System** - Environment fallback reflections
✅ **Shader Integration** - water.frag with `useSSRCubemap = 1`

### What's Disabled
❌ **Ocean** - FFT system disabled
❌ **Lake** - Simple blue water disabled

---

## How It Works

### Rendering Pipeline

```
Frame Start
    ↓
RenderingManager::update()
    ├─ Gets scene buffers from PostProcessor
    │   ├─ Scene color (Tex_Scene)
    │   ├─ Scene depth (Tex_Depth)
    │   └─ Scene normals (Tex_Normal)
    ↓
SSRManager::setInputTextures(color, depth, normal)
SSRManager::update(viewMatrix, projMatrix)
    ├─ Performs screen-space raymarching
    └─ Outputs SSR texture (RGB=reflection, A=confidence)
    ↓
CubemapManager::update(dt, cameraPos)
    └─ Updates cubemaps (if any exist)
    ↓
ShaderWaterStateSetUpdater::apply()
    ├─ Binds SSR texture → unit 5
    ├─ Binds cubemap → unit 6
    ├─ Binds normal map → unit 0
    └─ Binds reflection RTT → unit 1
    ↓
water.frag shader executes
    ├─ Samples ssrTexture (unit 5)
    ├─ Samples environmentMap (unit 6)
    ├─ Blends based on SSR confidence
    └─ Falls back to reflection RTT if SSR fails
```

---

## Expected Visual Results

### ✅ If SSR Works
- **Sharp reflections** of nearby terrain, rocks, buildings
- **Dynamic reflections** that update as you move
- **Sky fallback** from cubemap when SSR misses
- **Smooth blending** between SSR and cubemap

### ⚠️ If SSR Has Issues
- **Old reflection RTT** (planar reflection, slightly distorted)
- **No SSR** if textures aren't bound properly
- **Black reflections** if cubemap is null

### ❌ If Nothing Works
- **Flat blue/grey water** with no reflections
- Check console for shader errors
- Verify textures are bound (add debug logging)

---

## Debugging

### 1. Verify Shader Compilation
**Look for console output:**
```
Water shader compiled with useSSRCubemap = 1
```

### 2. Check Texture Binding
**In ShaderWaterStateSetUpdater::apply():**
```cpp
if (ssrMgr && ssrMgr->getResultTexture()) {
    std::cout << "SSR texture bound to unit 5\n";
}

osg::TextureCubeMap* cubemap = mWater->getCubemapForPosition(waterPos);
if (cubemap) {
    std::cout << "Cubemap bound to unit 6\n";
} else {
    std::cout << "WARNING: No cubemap available!\n";
}
```

### 3. Verify SSR Input Textures
**In renderingmanager.cpp update():**
```cpp
if (depthTex && sceneTex) {
    std::cout << "SSR receiving scene buffers\n";
} else {
    std::cerr << "ERROR: SSR missing input textures!\n";
}
```

### 4. Check Cubemap Region Creation
**In changeCell():**
```cpp
std::cout << "Cubemap regions: " << mCubemapManager->getRegionCount() << "\n";
```

---

## Known Limitations

### 1. No Cubemap Yet
**Problem:** Cubemaps aren't rendering yet (no scene capture)

**Result:** `environmentMap` will be null or black

**Impact:** SSR will work, but fallback will fail → may see artifacts

**Solution:** Implement cubemap rendering in CubemapReflectionManager

### 2. SSR Needs Scene Buffers
**Problem:** SSR requires depth/color from previous frame

**Result:** First frame may have no SSR

**Impact:** Brief flash of old reflection on scene load

**Solution:** This is expected, SSR starts on frame 2

### 3. Performance
**Problem:** SSR raymarching is expensive (128 steps default)

**Result:** May see FPS drop on slower GPUs

**Solution:** Tune SSR params (reduce max steps)

---

## What to Test

### Basic Functionality
1. **Load game** and go to any exterior with water
2. **Look at water** - should see reflections
3. **Move camera** - reflections should update
4. **Compare** - old planar vs. SSR (if working)

### Visual Quality Tests
1. **Stand on shore** looking at water
   - Should reflect nearby terrain sharply
   - Sky should be visible in distance

2. **Look straight down at water**
   - Should reflect ground beneath you
   - SSR confidence should be high

3. **Look at water at steep angle**
   - SSR may miss, cubemap should fill in
   - Should see smooth transition

### Performance Tests
1. **Check FPS** with SSR vs. without
2. **Monitor GPU usage** (MSI Afterburner, etc.)
3. **Test on different hardware** if available

---

## Fallback Behavior

The shader has **three reflection modes**:

### 1. SSR (Primary)
```glsl
if (ssrConfidence > 0.1) {
    reflection = mix(cubemapSample, ssrSample.rgb, ssrConfidence);
}
```
**Used when:** SSR successfully hits geometry

### 2. Cubemap (Fallback)
```glsl
else {
    reflection = cubemapSample;
}
```
**Used when:** SSR misses or confidence is low

### 3. Planar Reflection (Legacy)
```glsl
#else
    vec3 reflection = sampleReflectionMap(screenCoords + screenCoordsOffset).rgb;
#endif
```
**Used when:** `useSSRCubemap = 0` (old path)

**Current Setup:** Should use modes 1 and 2 (SSR + Cubemap hybrid)

---

## Success Criteria

### ✅ Minimum Success
- Water renders
- No crashes
- Some form of reflection visible

### ✅ Partial Success
- SSR works but cubemap is black
- Reflections visible but low quality
- Performance acceptable

### ✅ Full Success
- SSR + cubemap hybrid working
- Sharp local reflections
- Smooth fallback to sky
- 60+ FPS maintained

---

## Next Steps After Testing

### If SSR Works
1. Implement cubemap rendering (scene capture)
2. Tune SSR parameters for quality/performance
3. Add settings UI for SSR enable/quality
4. Re-enable Ocean/Lake with proper masking

### If SSR Doesn't Work
1. Add debug logging to trace issue
2. Check shader compilation errors
3. Verify texture binding order
4. Test with SSR disabled to isolate problem

### If Performance is Bad
1. Reduce SSR max steps (128 → 64)
2. Lower SSR resolution (half-res)
3. Add distance-based SSR fadeout
4. Profile GPU performance

---

## Build and Test

```bash
# Build
cmake --build build --target openmw -j8

# Run
./build/Debug/openmw.exe

# Watch for console output:
# - "SSR texture bound to unit 5"
# - "Cubemap bound to unit 6"
# - "SSR receiving scene buffers"

# Visual test:
# 1. Load save game
# 2. Find water
# 3. Move camera around water
# 4. Look for reflections changing
```

---

## Current Status

**Phase:** Ready for visual testing
**Expected:** Old water with SSR enabled
**Blockers:** None (should work)
**Next:** Build, run, observe results
