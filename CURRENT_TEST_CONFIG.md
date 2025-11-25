# Current Test Configuration

## What's Active

### Water Systems
- ✅ **Old Water Plane** - Rendering with full water.frag shader
- ✅ **SSR Integration** - Enabled via `mUseSSRReflections = true`
- ❌ **Ocean** - Disabled (`mUseOcean = false`)
- ❌ **Lake** - Disabled but uses simple shader (won't crash)

### Shader Configuration
**Old Water** uses `createShaderWaterStateSet()` with defines:
```cpp
defineMap["waterRefraction"] = mRefraction ? "1" : "0";
defineMap["rainRippleDetail"] = std::to_string(rippleDetail);
defineMap["rippleMapWorldScale"] = std::to_string(RipplesSurface::sWorldScaleFactor);  // 2.5
defineMap["rippleMapSize"] = std::to_string(RipplesSurface::sRTTSize) + ".0";           // 1024.0
defineMap["sunlightScattering"] = Settings::water().mSunlightScattering ? "1" : "0";
defineMap["wobblyShores"] = Settings::water().mWobblyShores ? "1" : "0";
defineMap["useSSRCubemap"] = mUseSSRReflections ? "1" : "0";  // "1" = SSR ENABLED!
```

### Texture Bindings (ShaderWaterStateSetUpdater)
- **Unit 0:** Normal map
- **Unit 1:** Reflection RTT
- **Unit 2:** Refraction RTT (if enabled)
- **Unit 3:** Refraction depth (if enabled)
- **Unit 4:** Ripple map
- **Unit 5:** SSR texture (if `mUseSSRReflections = true`)
- **Unit 6:** Cubemap (if `mUseSSRReflections = true`)

---

## What Should Happen

### Initialization Sequence
```
1. Ocean constructor → (disabled, won't render)
2. Lake constructor → simple shader (won't crash, won't render)
3. WaterHeightField init → tracking system ready
4. SSRManager init → SSR system ready
5. CubemapManager init → cubemap system ready
6. RippleSimulation init → ripples ready
7. Create old water geometry
8. updateWaterMaterial() called:
   a. Creates Reflection RTT
   b. Creates Refraction RTT (if enabled in settings)
   c. Creates Ripples RTT
   d. Calls createShaderWaterStateSet()
      - Compiles water.frag with useSSRCubemap=1
      - All defines provided (rippleMapSize, etc.)
   e. Attaches ShaderWaterStateSetUpdater
      - Binds SSR texture (unit 5)
      - Binds cubemap (unit 6)
```

### Runtime Behavior
```
Every Frame:
1. RenderingManager::update()
   - PostProcessor renders scene → color/depth/normal buffers
   - SSRManager receives buffers
   - SSRManager::update(viewMatrix, projMatrix)
     → Performs raymarching
     → Outputs SSR texture (RGB=reflection, A=confidence)
   - CubemapManager::update(dt, cameraPos)
     → Updates cubemaps (if any exist)

2. Water rendering:
   - ShaderWaterStateSetUpdater::apply()
     → Binds SSR texture
     → Binds cubemap
     → Binds other textures (normal, reflection, ripples)

3. water.frag shader executes:
   #if useSSRCubemap == 1
     vec4 ssrSample = texture2D(ssrTexture, screenCoords);
     vec3 cubemapSample = textureCube(environmentMap, reflectDir);

     if (ssrConfidence > 0.1) {
       reflection = mix(cubemapSample, ssrSample.rgb, ssrConfidence);
     } else {
       reflection = cubemapSample;
     }
   #else
     vec3 reflection = sampleReflectionMap(screenCoords);
   #endif
```

---

## Expected Visual Results

### Best Case
- ✅ SSR works: Sharp reflections of nearby terrain
- ✅ Cubemap works: Sky/environment fallback
- ✅ Hybrid blending: Smooth transition

### Good Case
- ✅ SSR works
- ⚠️ Cubemap is null/black (not yet rendered)
- ✅ Falls back to old reflection when SSR misses

### Acceptable Case
- ⚠️ SSR texture is null
- ✅ Falls back to old reflection RTT
- 🔍 Proves shader compiled correctly, debug texture binding

### Failure Case
- ❌ Crash on startup → Shader compilation failed
- ❌ No water → Geometry hidden
- ❌ Black screen → Something fundamentally wrong

---

## Previous Issue (FIXED)

### Problem
Lake was trying to compile water shader without all required defines:
```cpp
// BROKEN: Missing rippleMapSize, rippleMapWorldScale, etc.
defineMap["useSSRCubemap"] = "1";
program = shaderManager.getProgram("water", defineMap);
```

### Error
```
[12:56:10.376 E] Shader compatibility/water.vert error: Undefined rippleMapSize
[12:56:13.012 E] Fatal error: failed initializing shader: water
```

### Solution
Lake now uses simple shader:
```cpp
// FIXED: Use dedicated simple shader
auto vert = shaderManager.getShader("lake.vert", {}, osg::Shader::VERTEX);
auto frag = shaderManager.getShader("lake.frag", {}, osg::Shader::FRAGMENT);
```

Lake doesn't render anyway (disabled), so this just prevents crash.

---

## Build & Test

```bash
# Build
cmake --build build --target openmw -j8

# Expected: Success (no shader errors)

# Run
./build/Release/openmw.exe

# Expected: Game loads, water visible
```

### Test Checklist

- [ ] Game loads without crash
- [ ] Water plane visible
- [ ] Reflections present (any kind)
- [ ] Move camera → reflections update
- [ ] Check FPS (compare to baseline)
- [ ] Look for console warnings

---

## Debugging Aids

### Add to renderingmanager.cpp (line 920)
```cpp
std::cout << "SSR Manager: " << (ssrMgr ? "EXISTS" : "NULL") << "\n";
std::cout << "SSR Texture: " << (ssrMgr && ssrMgr->getResultTexture() ? "EXISTS" : "NULL") << "\n";
```

### Add to water.cpp ShaderWaterStateSetUpdater::apply() (line 700)
```cpp
std::cout << "SSR confidence texture bound to unit 5\n";

osg::TextureCubeMap* cubemap = mWater->getCubemapForPosition(waterPos);
std::cout << "Cubemap: " << (cubemap ? "EXISTS" : "NULL") << "\n";
```

### Check shader compilation
Look for console output around water initialization:
```
[timestamp] Compiling shader: water
[timestamp] Shader defines: waterRefraction=1 rippleMapSize=1024.0 ... useSSRCubemap=1
```

---

## Status

**Ready for testing!**

Lake shader fixed → Should not crash
Old water + SSR → Should render with SSR enabled
