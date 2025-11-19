# AI Implementation Prompt: FFT Ocean Integration for OpenMW

## Context

You are working on the OpenMW game engine (open-source Morrowind reimplementation). The codebase has an **80% complete FFT ocean system** inspired by GodotOceanWaves. Your task is to finish the integration and make it production-ready.

**DO NOT REIMPLEMENT FROM SCRATCH.** The FFT compute pipeline, cascade system, and shaders already exist and are working. You only need to wire them together.

---

## Background Reading

Before starting, read these files to understand the existing implementation:

1. **Audit Report:** `docs/FFT_OCEAN_AUDIT.md` (comprehensive analysis)
2. **FFT Simulation:** `components/ocean/oceanfftsimulation.cpp` (687 lines - ALREADY WORKING)
3. **Ocean Renderer:** `apps/openmw/mwrender/oceanwaterrenderer.cpp` (264 lines - NEEDS FIXING)
4. **Compute Shaders:** `files/shaders/core/ocean/fft_*.comp` (4 shaders - ALREADY WORKING)
5. **Rendering Shaders:** `files/shaders/compatibility/ocean/ocean.vert/frag` (NEED HOOKUP)

---

## What Already Works (DO NOT TOUCH)

These components are **fully implemented and functional**:

✅ **FFT Compute Pipeline** (`oceanfftsimulation.cpp`)
- Phillips spectrum generation
- Time evolution shader (fft_update_spectrum.comp)
- Cooley-Tukey FFT butterfly (fft_butterfly.comp)
- Displacement & normal generation (fft_generate_displacement.comp)
- Foam accumulation (fft_update_foam.comp)
- Multi-cascade system (3 cascades, 50m/200m/800m tile sizes)

✅ **OpenSceneGraph Integration**
- Compute shader support detection (GL 4.3+)
- Texture creation (RGBA32F formats)
- Butterfly texture precomputation
- State management

✅ **Water Manager** (`watermanager.cpp`)
- Water type classification (ocean/lake/river/indoor)
- FFT enable/disable logic
- Cell change handling

---

## What Needs Implementation (YOUR TASKS)

### PRIMARY TASK: Fix Shader Integration

**File:** `apps/openmw/mwrender/oceanwaterrenderer.cpp`
**Function:** `setupOceanShader()` (lines 212-241)

**Current State:**
```cpp
// WRONG: Using simple blue material for testing
void OceanWaterRenderer::setupOceanShader() {
    mOceanStateSet = new osg::StateSet;

    // Simple test material (no FFT)
    osg::ref_ptr<osg::Material> material = new osg::Material;
    material->setDiffuse(...);
    // ...
}
```

**Required Implementation:**
```cpp
void OceanWaterRenderer::setupOceanShader() {
    Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();

    // 1. Load ocean shaders
    Shader::ShaderManager::DefineMap defineMap;
    defineMap["radialFog"] = "1";
    defineMap["disableNormals"] = "0";

    mOceanProgram = shaderMgr.getProgram("compatibility/ocean/ocean", defineMap);

    if (!mOceanProgram) {
        Log(Debug::Error) << "Failed to load ocean shaders, falling back to simple mode";
        // Keep existing simple material as fallback
        return;
    }

    // 2. Create state set
    mOceanStateSet = new osg::StateSet;
    mOceanStateSet->setAttributeAndModes(mOceanProgram, osg::StateAttribute::ON);

    // 3. Bind FFT textures (3 cascades)
    if (mFFTSimulation) {
        for (int i = 0; i < mFFTSimulation->getCascadeCount(); i++) {
            // Displacement textures (units 0-2)
            osg::Texture2D* dispTex = mFFTSimulation->getDisplacementTexture(i);
            if (dispTex) {
                mOceanStateSet->setTextureAttributeAndModes(i, dispTex, osg::StateAttribute::ON);
                std::string uniformName = "uDisplacementCascade" + std::to_string(i);
                mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), i));
            }

            // Normal textures (units 3-5)
            osg::Texture2D* normalTex = mFFTSimulation->getNormalTexture(i);
            if (normalTex) {
                int unit = 3 + i;
                mOceanStateSet->setTextureAttributeAndModes(unit, normalTex, osg::StateAttribute::ON);
                std::string uniformName = "uNormalCascade" + std::to_string(i);
                mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), unit));
            }

            // Foam textures (units 6-8)
            osg::Texture2D* foamTex = mFFTSimulation->getFoamTexture(i);
            if (foamTex) {
                int unit = 6 + i;
                mOceanStateSet->setTextureAttributeAndModes(unit, foamTex, osg::StateAttribute::ON);
                std::string uniformName = "uFoamCascade" + std::to_string(i);
                mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), unit));
            }

            // Cascade tile sizes
            float tileSize = mFFTSimulation->getCascadeTileSize(i);
            std::string sizeUniformName = "uCascadeTileSize" + std::to_string(i);
            mOceanStateSet->addUniform(new osg::Uniform(sizeUniformName.c_str(), tileSize));
        }
    }

    // 4. Set shader uniforms
    mOceanStateSet->addUniform(new osg::Uniform("uTime", 0.0f));
    mOceanStateSet->addUniform(new osg::Uniform("uWaveAmplitude", 1.0f));
    mOceanStateSet->addUniform(new osg::Uniform("uEnableOceanWaves", true));

    // 5. Setup rendering state
    mOceanStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    mOceanStateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
    mOceanStateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

    osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc(
        osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ONE_MINUS_SRC_ALPHA);
    mOceanStateSet->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

    osg::ref_ptr<osg::Depth> depth = new osg::Depth;
    depth->setWriteMask(false);
    depth->setFunction(osg::Depth::LEQUAL);
    mOceanStateSet->setAttributeAndModes(depth, osg::StateAttribute::ON);

    Log(Debug::Info) << "Ocean FFT shader setup complete with "
                     << mFFTSimulation->getCascadeCount() << " cascades";
}
```

**Verify in update():**
```cpp
void OceanWaterRenderer::update(float dt, const osg::Vec3f& playerPos) {
    if (!mEnabled)
        return;

    // Update time uniform
    static float accumulatedTime = 0.0f;
    accumulatedTime += dt;

    osg::Uniform* timeUniform = mOceanStateSet->getUniform("uTime");
    if (timeUniform)
        timeUniform->set(accumulatedTime);

    // Update camera position
    osg::Uniform* camPosUniform = mOceanStateSet->getUniform("uCameraPosition");
    if (camPosUniform)
        camPosUniform->set(playerPos);

    // Position clipmap
    float meshSize = 8192.0f;
    float gridSize = 256.0f;
    float vertexSpacing = meshSize / gridSize;

    float snappedX = std::floor(playerPos.x() / vertexSpacing) * vertexSpacing;
    float snappedY = std::floor(playerPos.y() / vertexSpacing) * vertexSpacing;

    mClipmapTransform->setPosition(osg::Vec3f(snappedX, snappedY, mWaterHeight));
}
```

---

### SECONDARY TASK: Connect Compute Dispatch

**Problem:** FFT compute shaders exist but are never executed. Textures are created but never updated.

**Solution:** Call `dispatchCompute()` from the rendering thread with a valid OpenGL state.

**Option 1: Cull Callback (Recommended)**

Add to `oceanwaterrenderer.hpp`:
```cpp
class OceanUpdateCallback : public osg::NodeCallback {
public:
    OceanUpdateCallback(Ocean::OceanFFTSimulation* fft) : mFFT(fft) {}

    virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
        if (nv->getVisitorType() == osg::NodeVisitor::CULL_VISITOR) {
            osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(nv);
            osg::State* state = cv->getState();

            if (state && mFFT) {
                mFFT->dispatchCompute(state);
            }
        }
        traverse(node, nv);
    }

private:
    Ocean::OceanFFTSimulation* mFFT;
};
```

In `OceanWaterRenderer` constructor:
```cpp
OceanWaterRenderer::OceanWaterRenderer(...) {
    // ... existing code ...

    // Add compute callback
    if (mFFTSimulation) {
        mOceanNode->setCullCallback(new OceanUpdateCallback(mFFTSimulation));
        Log(Debug::Info) << "Ocean FFT compute callback installed";
    }
}
```

**Option 2: Pre-Draw Callback (Alternative)**

```cpp
class OceanPreDrawCallback : public osg::Camera::DrawCallback {
public:
    OceanPreDrawCallback(Ocean::OceanFFTSimulation* fft) : mFFT(fft) {}

    virtual void operator()(osg::RenderInfo& renderInfo) const {
        osg::State* state = renderInfo.getState();
        if (state && mFFT) {
            mFFT->dispatchCompute(state);
        }
    }

private:
    Ocean::OceanFFTSimulation* mFFT;
};

// Install on main camera (need to find camera reference)
mainCamera->setPreDrawCallback(new OceanPreDrawCallback(mFFTSimulation));
```

**Verification:**
```cpp
// Check logs for:
// [OCEAN FFT] First compute dispatch - FFT simulation is running
// [OCEAN FFT] Compute dispatch #100 - Time: 5.0s
```

---

### TERTIARY TASK: Verify & Debug

**Use apitrace or RenderDoc to verify:**

1. **Compute Shaders Executing**
   - Look for `glDispatchCompute()` calls
   - Should see 21 dispatches per cascade per update
   - Work groups: (32, 32, 1) for 512×512 resolution

2. **Textures Updating**
   - Displacement maps should show colorful patterns
   - Normal maps should be bluish with variations
   - Foam maps should have white patches

3. **Shader Bindings**
   - Check that textures are bound to correct units (0-8)
   - Verify uniforms are set (uTime, uCascadeTileSize0/1/2)

4. **Visual Result**
   - Ocean surface should have animated waves
   - Normals should affect lighting
   - Foam should appear at wave crests

**Debug Commands:**

```cpp
// Add to update() for debugging:
if (frameCount % 60 == 0) {  // Every 60 frames
    Log(Debug::Info) << "[OCEAN] FFT Cascade 0 displacement texture: "
                     << (mFFTSimulation->getDisplacementTexture(0) ? "VALID" : "NULL");
    Log(Debug::Info) << "[OCEAN] Ocean program: "
                     << (mOceanProgram ? "VALID" : "NULL");
    Log(Debug::Info) << "[OCEAN] Clipmap vertices: "
                     << mClipmapGeometry->getVertexArray()->getNumElements();
}
```

---

## Testing Checklist

After implementation, test these scenarios:

### Functional Tests
- [ ] Ocean visible in Bitter Coast exterior cells
- [ ] Waves animating smoothly
- [ ] No crashes or errors in logs
- [ ] FFT compute shaders dispatching (check logs)
- [ ] Textures updating (use RenderDoc)
- [ ] Normals affecting lighting correctly
- [ ] Foam appearing at wave crests

### Visual Tests
- [ ] No seams between cascades
- [ ] Distance attenuation working
- [ ] Proper depth sorting (no z-fighting)
- [ ] Reflections look reasonable (Fresnel)
- [ ] Water color appropriate
- [ ] Transparency working

### Performance Tests
- [ ] 60 FPS on RTX 2060 (HIGH preset)
- [ ] GPU time < 5ms for ocean rendering
- [ ] No stuttering or frame drops
- [ ] Memory usage reasonable (~70 MB VRAM)

### Edge Cases
- [ ] Cell transitions (ocean ↔ interior)
- [ ] Fast travel doesn't break ocean
- [ ] Save/load preserves state
- [ ] Weather changes don't cause issues

---

## Common Pitfalls to Avoid

### ❌ Don't Reimplement Existing Code
The FFT pipeline is DONE. Don't touch:
- `oceanfftsimulation.cpp` (except to call its methods)
- Compute shaders (they're correct)
- Cascade system (works as-is)

### ❌ Don't Change Shader Uniforms
The ocean.vert/ocean.frag shaders expect specific uniforms:
- `uDisplacementCascade0/1/2`
- `uNormalCascade0/1/2`
- `uFoamCascade0/1/2`
- `uCascadeTileSize0/1/2`
- `uTime`, `uWaveAmplitude`, `uEnableOceanWaves`

Match these exactly or shaders won't work.

### ❌ Don't Call dispatchCompute() from Wrong Thread
MUST be called from rendering thread with valid `osg::State*`. Use callbacks, not from `update()` directly.

### ❌ Don't Skip Texture Binding
All 9 textures must be bound (3 displacement + 3 normal + 3 foam). Missing bindings = black ocean.

### ❌ Don't Assume Immediate Results
FFT cascades update staggered (1 per frame). First visible waves may take 3 frames.

---

## Performance Optimization (Later)

**Don't optimize prematurely!** Get it working first. But when needed:

### If FPS Too Low:
1. Reduce cascade count (3 → 2)
2. Lower resolution (512 → 256)
3. Increase update interval (0.05 → 0.1)
4. Disable distant cascades

### If Memory Too High:
1. Use lower resolution
2. Reduce cascade count
3. Compress foam texture (R32F → R16F)

### If Compute Too Slow:
1. Profile each shader stage
2. Likely bottleneck: FFT butterfly passes
3. Consider Stockham algorithm (10% faster)
4. Reduce butterfly texture lookups

---

## Success Criteria

**Minimum Viable Product:**
- ✅ Ocean visible with animated waves
- ✅ FFT displacement applied to vertices
- ✅ Normals affecting lighting
- ✅ Foam at wave crests
- ✅ 60 FPS on mid-range GPU

**Stretch Goals:**
- ⚠️ Environment map reflections
- ⚠️ PBR material (GGX instead of Blinn-Phong)
- ⚠️ Subsurface scattering
- ⚠️ Parameter controls (wind, choppiness)

---

## Key Files Reference

**Must Modify:**
1. `apps/openmw/mwrender/oceanwaterrenderer.cpp` - setupOceanShader() + add callback
2. `apps/openmw/mwrender/oceanwaterrenderer.hpp` - Add callback class

**Must NOT Modify:**
1. `components/ocean/oceanfftsimulation.cpp` - Already perfect
2. `files/shaders/core/ocean/*.comp` - Already correct
3. `files/shaders/compatibility/ocean/ocean.vert` - Shader ready to use
4. `files/shaders/compatibility/ocean/ocean.frag` - Shader ready to use

**May Modify Later:**
1. `apps/openmw/mwrender/watermanager.cpp` - For parameter controls
2. `components/ocean/oceanfftsimulation.hpp` - For TMA spectrum

---

## Debugging Tips

### If Ocean Not Visible:
1. Check node mask: `mOceanNode->getNodeMask()` should be `Mask_Water`
2. Check enabled: `mEnabled` should be true
3. Check geometry: `mClipmapGeometry` should have 65,536 vertices
4. Check position: `mClipmapTransform->getPosition()` near player
5. Check shaders: `mOceanProgram` should not be null

### If Ocean Flat (No Waves):
1. Check compute dispatch: Look for logs `[OCEAN FFT] First compute dispatch`
2. Check textures: Use RenderDoc to inspect displacement maps
3. Check uniforms: `uWaveAmplitude` should be 1.0, `uEnableOceanWaves` true
4. Check texture binding: All 9 texture units should have textures

### If Ocean Black:
1. Check shader compilation: Look for shader errors in logs
2. Check texture binding: Missing textures → black
3. Check lighting: Normals might be inverted

### If Performance Bad:
1. Profile GPU: Should be ~2-5ms for HIGH preset
2. Check resolution: 1024×1024 is too high for most GPUs
3. Check cascade count: 3 cascades is expensive, try 2
4. Check update interval: Should be 0.05-0.1s, not every frame

---

## Resources

**Documentation:**
- FFT Ocean Audit: `docs/FFT_OCEAN_AUDIT.md`
- OpenMW Shader Guide: (search codebase)
- GodotOceanWaves: https://github.com/2Retr0/GodotOceanWaves

**Reference Implementations:**
- Tessendorf 2001 Paper: "Simulating Ocean Water" (FFT ocean classic)
- GPU Gems Chapter 1: "Effective Water Simulation from Physical Models"

**Tools:**
- RenderDoc: GPU debugging, texture inspection
- apitrace: OpenGL call tracing
- osgviewer: Test OSG scene graphs standalone

---

## Final Notes

**This is an integration task, not a research task.** Everything you need is already implemented:
- ✅ FFT math is correct
- ✅ Shaders are written
- ✅ Textures are created
- ✅ Geometry is generated

**You just need to connect the dots:**
1. Load ocean shaders instead of test material
2. Bind FFT textures to shader
3. Call compute dispatch from render thread

**Estimated Time:** 4-8 hours for experienced developer

**If stuck:** Re-read `docs/FFT_OCEAN_AUDIT.md` section 6 (Implementation Roadmap)

**Good luck!** You're implementing one of the most sophisticated ocean rendering systems in an open-source game engine. The hard work is done—you just need to flip the switch. 🌊
