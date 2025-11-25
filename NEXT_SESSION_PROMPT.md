# Next Session: SSR + Cubemap Full Integration

## CONTEXT

I'm implementing a modern multi-altitude water system for OpenMW that supports:
- **Ocean** (FFT-based, sea level)
- **Lakes** (multiple, at different altitudes)
- **Rivers** (future)

**Current Status:**
- ✅ **Phase 1 COMPLETE:** Water Height Field system (2048×2048 texture tracking water altitude/type)
- 🟡 **Phase 2 INFRASTRUCTURE COMPLETE:** SSR + Cubemap reflection system (code built, NOT integrated)

---

## WHAT WAS JUST IMPLEMENTED (Session 2025-11-25)

### Water Height Field System ✅
- Fixed compilation errors (API compatibility)
- System is **production ready** and working
- Tracks water across loaded cells with Ocean/Lake/River classification

### SSR + Cubemap Infrastructure 🟡
Built complete infrastructure but **NOT visually integrated** yet:

**New Classes Created:**
- `SSRManager` ([ssrmanager.hpp/cpp](apps/openmw/mwrender/ssrmanager.hpp)) - Screen-space reflection manager
- `CubemapReflectionManager` ([cubemapreflection.hpp/cpp](apps/openmw/mwrender/cubemapreflection.hpp)) - Environment cubemap manager

**New Shader:**
- `ssr_raymarch.frag` ([ssr_raymarch.frag](files/shaders/compatibility/ssr_raymarch.frag)) - SSR raymarch implementation

**Integration Status:**
- ✅ Added to CMakeLists.txt
- ✅ Initialized in WaterManager constructor
- ✅ Update calls in WaterManager::update()
- ❌ **NOT connected to scene rendering** (no input textures)
- ❌ **NOT used by water shader** (no visual output)
- ❌ **NO cubemap regions created** (nothing renders)

**Why Not Working Visually:**
1. SSR needs scene color/depth buffers → Not connected to RenderingManager
2. Water shader doesn't sample SSR/cubemap textures → Still uses old reflection
3. No cubemap regions placed → Cubemaps exist but aren't populated

---

## YOUR TASK: FULL INTEGRATION

Make the SSR + Cubemap system **visually testable** by completing the rendering pipeline integration.

### Required Steps

#### 1. Connect SSR to Scene Rendering
**File:** `apps/openmw/mwrender/renderingmanager.cpp`

- Get scene color/depth/normal buffers from main rendering pass
- Pass them to SSRManager via `setInputTextures()`
- Update SSR matrices each frame in camera update

**Code Location to Modify:**
Look for where `mWater->update()` is called and add:
```cpp
if (mWater && mWater->getSSRManager()) {
    auto* ssrMgr = mWater->getSSRManager();
    ssrMgr->setInputTextures(sceneColorBuffer, sceneDepthBuffer, normalBuffer);
    ssrMgr->update(viewMatrix, projectionMatrix);
}
```

**Challenges:**
- Need to expose SSRManager getter in water.hpp
- May need to create scene buffers if they don't exist
- Normal buffer is optional (can pass nullptr)

#### 2. Update Water Shader to Use SSR + Cubemap
**File:** `files/shaders/compatibility/water.frag`

**Current Code (around line 156-157):**
```glsl
// reflection
vec3 reflection = sampleReflectionMap(screenCoords + screenCoordsOffset).rgb;
```

**Replace With:**
```glsl
// SSR + Cubemap hybrid reflection
vec3 reflection;

#if USE_SSR_CUBEMAP
    // Sample SSR (RGB = color, A = confidence)
    vec4 ssrSample = texture2D(ssrTexture, screenCoords);

    // Sample cubemap as fallback
    vec3 reflectDir = reflect(viewDir, normal);
    vec3 cubemapSample = textureCube(environmentMap, reflectDir).rgb;

    // Blend based on SSR confidence
    float ssrConfidence = ssrSample.a;
    if (ssrConfidence > 0.1) {
        reflection = mix(cubemapSample, ssrSample.rgb, ssrConfidence);
    } else {
        reflection = cubemapSample;
    }
#else
    // Fallback to old planar reflection
    reflection = sampleReflectionMap(screenCoords + screenCoordsOffset).rgb;
#endif
```

**Add Uniforms at Top:**
```glsl
#if USE_SSR_CUBEMAP
uniform sampler2D ssrTexture;
uniform samplerCube environmentMap;
#endif
```

**Shader Define:**
Add to water shader creation in `water.cpp`:
```cpp
defineMap["USE_SSR_CUBEMAP"] = mUseSSRReflections ? "1" : "0";
```

#### 3. Bind SSR and Cubemap Textures to Water
**File:** `apps/openmw/mwrender/water.cpp`

**In `ShaderWaterStateSetUpdater::setDefaults()` or `apply()`:**
```cpp
if (mWater->useSSRReflections()) {
    // Bind SSR result texture
    stateset->addUniform(new osg::Uniform("ssrTexture", 5));
    stateset->setTextureAttributeAndModes(5,
        mWater->getSSRManager()->getResultTexture(),
        osg::StateAttribute::ON);

    // Bind cubemap
    stateset->addUniform(new osg::Uniform("environmentMap", 6));
    osg::TextureCubeMap* cubemap = mWater->getCubemapForPosition(cameraPos);
    if (cubemap) {
        stateset->setTextureAttributeAndModes(6, cubemap,
            osg::StateAttribute::ON);
    }
}
```

**Challenges:**
- Need to expose getSSRManager() and getCubemapForPosition() in water.hpp
- Texture unit conflicts (check existing units 0-4)

#### 4. Create Test Cubemap Regions
**File:** `apps/openmw/mwrender/water.cpp`

**In `WaterManager::changeCell()`:**
```cpp
// Create cubemap region for lakes/rivers
if (!isOcean && mCubemapManager) {
    // Use cell center as cubemap position
    osg::Vec3f cubemapCenter = getSceneNodeCoordinates(
        store->getCell()->getGridX(),
        store->getCell()->getGridY()
    );

    // Check if region already exists near this position
    // If not, create new one with ~1000 unit radius
    if (mCubemapManager->getRegionCount() < 8) { // Max 8 cubemaps
        mCubemapManager->addRegion(cubemapCenter, 1000.0f);
    }
}
```

**Better Approach:**
Use WaterHeightField to query water type and place cubemaps intelligently:
```cpp
WaterType type = mWaterHeightField->sampleType(pos);
if (type == WaterType::Lake || type == WaterType::River) {
    // Place cubemap
}
```

#### 5. Add Public Accessors to WaterManager
**File:** `apps/openmw/mwrender/water.hpp`

Add to public section:
```cpp
// SSR + Cubemap accessors
SSRManager* getSSRManager() { return mSSRManager.get(); }
CubemapReflectionManager* getCubemapManager() { return mCubemapManager.get(); }
bool useSSRReflections() const { return mUseSSRReflections; }
osg::TextureCubeMap* getCubemapForPosition(const osg::Vec3f& pos);
```

Implement `getCubemapForPosition()` in water.cpp:
```cpp
osg::TextureCubeMap* WaterManager::getCubemapForPosition(const osg::Vec3f& pos)
{
    if (mCubemapManager)
        return mCubemapManager->getCubemapForPosition(pos);
    return nullptr;
}
```

---

## TESTING AFTER INTEGRATION

### Build & Run
```bash
cd "d:\Gamedev\openmw-snow"
cmake --build build --target openmw
./build/Debug/openmw.exe
```

### Test Locations in Morrowind

1. **Vivec City** - Canton water (lake-level, should use SSR+cubemap)
2. **Balmora** - Odai River (lake-type water)
3. **Bitter Coast** - Various small lakes at different altitudes
4. **Seyda Neen** - Near ocean, test ocean vs lake transition

### What to Look For

**Visual Checks:**
- ✅ Water reflects nearby buildings/terrain (SSR)
- ✅ Water reflects sky and distant environment (cubemap)
- ✅ Smooth blending between SSR and cubemap
- ✅ No black/missing reflections
- ✅ Reflections fade at screen edges

**Performance Checks:**
- Monitor FPS (should drop <10% with SSR enabled)
- SSR cost: ~0.5-1.5ms per frame
- Cubemap cost: <0.1ms per frame

**Debug Output:**
Check console for:
- "SSR + Cubemap system initialized!"
- "Cubemap regions active: X"

### Common Issues & Fixes

**Issue:** Black reflections
→ Check SSR input textures are bound
→ Verify SSR shader compiles without errors

**Issue:** No reflections at all
→ Verify USE_SSR_CUBEMAP define is set
→ Check texture bindings in water shader

**Issue:** Cubemap not updating
→ Verify cubemap regions are created
→ Check mCubemapManager->update() is called

**Issue:** Performance drop >15%
→ Reduce SSR maxSteps (128 → 64)
→ Reduce cubemap resolution (512 → 256)

---

## FILES TO MODIFY

**Must Modify:**
1. `apps/openmw/mwrender/renderingmanager.cpp` - Connect scene buffers
2. `apps/openmw/mwrender/water.hpp` - Add accessors
3. `apps/openmw/mwrender/water.cpp` - Bind textures, create regions, add getCubemapForPosition()
4. `files/shaders/compatibility/water.frag` - SSR+cubemap sampling

**May Need to Check:**
- `apps/openmw/mwrender/renderingmanager.hpp` - Scene buffer availability
- `files/shaders/compatibility/water.vert` - Pass data to fragment shader

---

## EXPECTED OUTCOME

After full integration:
- ✅ SSR renders reflections in screen space
- ✅ Cubemaps provide environment fallback
- ✅ Water shader blends both based on confidence
- ✅ Visually testable in-game
- ✅ Performance acceptable (<2ms overhead)

---

## FALLBACK PLAN

If full integration is too complex or time-consuming:

**Minimal Testable Version:**
1. Skip SSR (too complex, needs RenderingManager changes)
2. Just hook up cubemap to water shader
3. Create 1 test cubemap at world origin
4. **Result:** See basic cubemap reflections on lakes (no SSR, but visible)

This would take ~30 minutes vs 2-3 hours for full integration.

---

## PROGRESS TRACKING

**Update this file:** [WATER_SYSTEM_PROGRESS.md](WATER_SYSTEM_PROGRESS.md)

Add new section:
```markdown
### Session [DATE] - SSR + Cubemap Integration

**Goal:** Full visual integration

**Completed:**
- [ ] Connected SSR to scene rendering
- [ ] Modified water.frag shader
- [ ] Bound SSR/cubemap textures
- [ ] Created test cubemap regions
- [ ] Tested in-game

**Trials & Errors:**
[Log any issues encountered and how you solved them]

**Final Status:**
[Working / Partially working / Not working - with details]
```

---

## IMPORTANT NOTES

1. **Bullet Link Error:** Ignore it - pre-existing build config issue, unrelated to water
2. **Incremental Testing:** Test after each step (shader changes, texture binding, etc.)
3. **Shader Compilation:** Check for GLSL errors in console on startup
4. **Performance:** Profile if FPS drops significantly

---

## REFERENCE FILES

**Current Infrastructure (Already Built):**
- [ssrmanager.hpp](apps/openmw/mwrender/ssrmanager.hpp)
- [ssrmanager.cpp](apps/openmw/mwrender/ssrmanager.cpp)
- [cubemapreflection.hpp](apps/openmw/mwrender/cubemapreflection.hpp)
- [cubemapreflection.cpp](apps/openmw/mwrender/cubemapreflection.cpp)
- [ssr_raymarch.frag](files/shaders/compatibility/ssr_raymarch.frag)

**Modified (Phase 1 + 2):**
- [water.hpp](apps/openmw/mwrender/water.hpp) - Managers declared
- [water.cpp](apps/openmw/mwrender/water.cpp) - Managers initialized
- [waterheightfield.cpp](apps/openmw/mwrender/waterheightfield.cpp) - Fixed API calls
- [CMakeLists.txt](apps/openmw/CMakeLists.txt) - Build integration

**To Modify (This Session):**
- `renderingmanager.cpp/hpp` - Scene buffer connection
- `water.frag` - Shader integration
- `water.cpp` - Texture binding & cubemap regions

---

Good luck with the integration! 🚀
