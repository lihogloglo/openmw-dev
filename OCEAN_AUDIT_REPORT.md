# Ocean Rendering System - Comprehensive Audit Report

**Branch**: `oceann`
**Date**: 2025-11-18
**Auditor**: Claude
**Reference Implementation**: [GodotOceanWaves](https://github.com/2Retr0/GodotOceanWaves/)

---

## Executive Summary

The FFT-based ocean rendering system has been implemented with the core architecture in place, but **is currently non-functional** due to critical issues in shader uniform binding, rendering state configuration, and visual material setup. The ocean plane renders, but without wave displacement, proper normals, transparency, or visual fidelity comparable to the reference implementation.

**Status**: 🔴 **CRITICAL - System Non-Functional**

---

## Critical Issues (Blocking)

### 1. ❌ Compute Shader Uniforms Never Set

**Location**: `components/ocean/oceanfftsimulation.cpp:162-255` (`dispatchCompute()`)

**Problem**: The FFT compute shaders expect uniforms but they are never set before dispatch.

**Expected Uniforms**:
- `fft_update_spectrum.comp` (line 14-17):
  - `uniform float uTime;`
  - `uniform float uTileSize;`
  - `uniform float uGravity;`

- `fft_butterfly.comp` (line 18-19):
  - `uniform int uStage;`
  - `uniform bool uHorizontal;`

- `fft_generate_displacement.comp` (line 16-19):
  - `uniform float uTileSize;`
  - `uniform float uChoppiness;`
  - `uniform int uN;`

**Current Code**:
```cpp
// Line 197-205 in oceanfftsimulation.cpp
state->applyAttribute(mSpectrumGeneratorProgram.get());
bindImage(cascade.spectrumTexture.get(), 0, GL_READ_ONLY_ARB);
bindImage(cascade.fftTemp1.get(), 1, GL_WRITE_ONLY_ARB);

// Set uniforms
mSpectrumGeneratorProgram->apply(*state);  // ❌ No uniforms actually set!

ext->glDispatchCompute(res / 16, res / 16, 1);
```

**Impact**: The compute shaders execute with **uninitialized uniform values**, producing garbage output. The FFT never actually computes meaningful wave data.

**Fix Required**:
```cpp
// Before each dispatch, set uniforms:
state->applyAttribute(mSpectrumGeneratorProgram.get());

// Set uniforms using osg::Uniform
osg::Uniform* timeUniform = new osg::Uniform("uTime", mSimulationTime);
osg::Uniform* tileSizeUniform = new osg::Uniform("uTileSize", cascade.tileSize);
osg::Uniform* gravityUniform = new osg::Uniform("uGravity", GRAVITY);

state->applyUniform(timeUniform);
state->applyUniform(tileSizeUniform);
state->applyUniform(gravityUniform);
```

---

### 2. ❌ Missing Alpha Blending Configuration

**Location**: `apps/openmw/mwrender/oceanwaterrenderer.cpp:267-323` (`setupOceanShader()`)

**Problem**: Water is rendered with alpha (`uWaterAlpha = 0.8`) but blending is never enabled in the state set.

**Current Code**:
```cpp
mOceanStateSet = new osg::StateSet;
mOceanStateSet->setAttributeAndModes(mOceanProgram, osg::StateAttribute::ON);
// ❌ No blending setup!
```

**Impact**: Water renders **opaque** despite having alpha values. You can't see through the water surface.

**Fix Required**:
```cpp
// Enable alpha blending
mOceanStateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
mOceanStateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

// Configure blend function
osg::BlendFunc* blendFunc = new osg::BlendFunc(
    osg::BlendFunc::SRC_ALPHA,
    osg::BlendFunc::ONE_MINUS_SRC_ALPHA
);
mOceanStateSet->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

// Enable depth testing but disable depth writing for transparency
osg::Depth* depth = new osg::Depth;
depth->setWriteMask(false);  // Don't write to depth buffer
mOceanStateSet->setAttributeAndModes(depth, osg::StateAttribute::ON);
```

---

### 3. ❌ No Reflection/Refraction Textures

**Location**: `files/shaders/compatibility/ocean/ocean.frag`

**Problem**: The fragment shader doesn't sample reflection or refraction textures like the original water system.

**Reference** (from `apps/openmw/mwrender/water.cpp`):
- Creates reflection RTT camera
- Creates refraction RTT camera
- Binds textures to shader uniforms
- Blends reflection/refraction based on Fresnel

**Current Implementation**: Only basic diffuse color with simple Fresnel tint (line 50-76 in ocean.frag).

**Impact**: Water looks **flat and unrealistic** - no environment reflections, no underwater refraction.

**Fix Required**:
1. Add RTT cameras for reflection/refraction in `OceanWaterRenderer`
2. Bind reflection/refraction textures to ocean shader
3. Update fragment shader to sample and blend these textures
4. Implement proper Fresnel-based blending

---

### 4. ❌ Missing Water Absorption/Scattering

**Location**: `files/shaders/compatibility/ocean/ocean.frag`

**Problem**: No depth-based water color absorption like in real water.

**Reference Implementation** (GodotOceanWaves):
- Uses Beer-Lambert law for light absorption
- Depth-dependent color shift (shallow = cyan, deep = dark blue)
- Subsurface scattering approximation

**Current Implementation**: Static color interpolation (line 53 in ocean.frag):
```glsl
vec3 waterColor = mix(uShallowWaterColor, uDeepWaterColor, 0.7);  // ❌ Constant!
```

**Impact**: Water has uniform color regardless of depth, looking artificial.

**Fix Required**:
```glsl
// Need depth texture from scene
uniform sampler2D uDepthTexture;

// Sample depth and calculate absorption
float sceneDepth = texture2D(uDepthTexture, screenUV).r;
float waterDepth = calculateWaterDepth(sceneDepth, vViewPos.z);

// Beer-Lambert absorption
vec3 absorptionCoeffs = vec3(0.45, 0.03, 0.01);  // RGB absorption rates
vec3 transmittance = exp(-absorptionCoeffs * waterDepth);
vec3 waterColor = uShallowWaterColor * transmittance + uDeepWaterColor * (1.0 - transmittance);
```

---

### 5. ❌ Cascade Tile Size Uniforms Not Initialized

**Location**: `apps/openmw/mwrender/oceanwaterrenderer.cpp:325-373` (`updateFFTTextures()`)

**Problem**: Cascade tile size uniforms are created in `updateFFTTextures()` but the vertex shader needs them **immediately** when first rendered.

**Current Flow**:
1. `setupOceanShader()` creates state set (line 267)
2. Chunks created and rendered (line 254)
3. `updateFFTTextures()` called later (line 325)
4. **First frame renders with missing uniforms!**

**Impact**: First frames (or all frames if update fails) have **undefined tile sizes**, causing incorrect UV sampling.

**Fix Required**: Move uniform initialization to `setupOceanShader()`:
```cpp
void OceanWaterRenderer::setupOceanShader()
{
    // ... existing code ...

    // Initialize cascade tile sizes immediately
    for (int i = 0; i < 3; ++i) {
        float tileSize = mFFTSimulation->getCascadeTileSize(i);
        std::string uniformName = "uCascadeTileSize" + std::to_string(i);
        mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), tileSize));
    }
}
```

---

### 6. ❌ Wave Amplitude Unrealistically High

**Location**: `apps/openmw/mwrender/oceanwaterrenderer.cpp:315`

**Problem**:
```cpp
mOceanStateSet->addUniform(new osg::Uniform("uWaveAmplitude", 100.0f));  // ❌ 100 units!
```

**Context**: Morrowind uses game units where ~1 unit ≈ 1 foot. This creates **100-foot tall waves**.

**Reference** (GodotOceanWaves): Wave amplitude typically 0.5 - 5.0 meters (1.5 - 15 feet) for realistic ocean.

**Impact**: If the system worked, waves would be **absurdly tall**, clipping through terrain and boats.

**Fix Required**:
```cpp
mOceanStateSet->addUniform(new osg::Uniform("uWaveAmplitude", 2.0f));  // ~2 feet = realistic
```

---

## Major Issues (High Priority)

### 7. ⚠️ FFT Butterfly Texture Binding Incorrect

**Location**: `components/ocean/oceanfftsimulation.cpp:216-221`

**Problem**: Butterfly texture bound as sampler, but shader expects it as `layout(binding = 2)`.

**Current Code**:
```cpp
auto butterflyIt = mButterflyTextures.find(res);
if (butterflyIt != mButterflyTextures.end())
{
    state->applyTextureAttribute(2, butterflyIt->second.get());  // ❌ Wrong for compute shader!
}
```

**Issue**: Compute shaders use **image bindings**, not texture units. Should use `glBindImageTexture()` or shader storage buffer.

**Fix Required**:
```cpp
// Bind as texture sampler (compute shaders CAN use samplers)
state->applyTextureAttribute(2, butterflyIt->second.get());
// Ensure texture is not bound as image
```

OR update shader to use `uniform sampler2D uButterflyTexture` instead of `layout(binding = 2)`.

---

### 8. ⚠️ No Foam Persistence/Dissipation

**Location**: `files/shaders/core/ocean/fft_generate_displacement.comp:88-92`

**Problem**: Foam is computed fresh each frame from Jacobian, no temporal persistence.

**Reference** (GodotOceanWaves):
- Foam accumulates over time where waves break (negative Jacobian)
- Dissipates exponentially with decay rate (~0.9 per second)
- Uses separate foam texture that's updated additively

**Current Implementation**:
```glsl
float jacobian = 1.0 - kLength * height * uChoppiness;
float foam = clamp(-jacobian, 0.0, 1.0);
imageStore(uFoamMap, coord, vec4(foam, 0.0, 0.0, 0.0));  // ❌ Overwrite, no persistence!
```

**Impact**: Foam flickers frame-to-frame instead of persisting on wave crests.

**Fix Required**: Add foam accumulation shader pass:
```glsl
// Previous foam
float oldFoam = imageLoad(uFoamMap, coord).r;

// New foam from breaking waves
float newFoam = clamp(-jacobian, 0.0, 1.0);

// Accumulate and decay
float foamGrowth = 0.2;    // How fast foam forms
float foamDecay = 0.95;    // Exponential decay per frame
float updatedFoam = clamp(oldFoam * foamDecay + newFoam * foamGrowth, 0.0, 1.0);

imageStore(uFoamMap, coord, vec4(updatedFoam, 0.0, 0.0, 0.0));
```

---

### 9. ⚠️ Missing Normal Map Tangent Space Calculation

**Location**: `files/shaders/compatibility/ocean/ocean.vert:92-94`

**Problem**: Normal sampled from FFT is already in world space, but vertex shader might need tangent-bitangent frame.

**Current Code**:
```glsl
vec3 displacedNormal = sampleNormal(worldPos.xy);
vNormal = displacedNormal;  // ❌ No transformation!
```

**Issue**: If later stages expect view-space or tangent-space normals, this will fail.

**Fix Required**: Verify normal space requirements and add proper transformation:
```glsl
// World space normal from FFT
vec3 worldNormal = sampleNormal(worldPos.xy);

// Transform to view space for lighting
vNormal = gl_NormalMatrix * worldNormal;
```

---

### 10. ⚠️ Choppiness Not Set in Displacement Shader

**Location**: `components/ocean/oceanfftsimulation.cpp:245-250`

**Problem**: Displacement generation shader never receives `uChoppiness` uniform.

**Expected** (from `oceanfftsimulation.hpp:80`):
```cpp
mChoppiness(2.5f)  // Set in constructor
```

**Actual**: Never passed to shader, uses **undefined value**.

**Impact**: Horizontal displacement (wave sharpness) is random/zero.

**Fix Required**: Set uniform before displacement dispatch:
```cpp
state->applyAttribute(mDisplacementProgram.get());

osg::Uniform* choppinessUniform = new osg::Uniform("uChoppiness", mChoppiness);
osg::Uniform* tileSizeUniform = new osg::Uniform("uTileSize", cascade.tileSize);
osg::Uniform* nUniform = new osg::Uniform("uN", res);

state->applyUniform(choppinessUniform);
state->applyUniform(tileSizeUniform);
state->applyUniform(nUniform);

// Then dispatch
ext->glDispatchCompute(res / 16, res / 16, 1);
```

---

## Medium Priority Issues

### 11. ⚠️ Spectrum Only Generated Once (No Time Evolution)

**Location**: `components/ocean/oceanfftsimulation.cpp:415-462` (`generateSpectrum()`)

**Problem**: Initial spectrum H0(k) is generated once with fixed seed. Time evolution happens in compute shader, BUT `uTime` is never set (see Issue #1).

**Current Flow**:
1. `generateSpectrum()` creates H0(k) once during init
2. `updateCascade()` should evolve spectrum with time (line 464-473)
3. BUT compute shader never receives time → **static waves**

**Expected**: Spectrum should animate based on dispersion relation ω = √(gk).

**Fix**: See Issue #1 - set `uTime` uniform in spectrum update dispatch.

---

### 12. ⚠️ No Depth Pre-Pass for Underwater Effects

**Location**: Missing in entire rendering pipeline

**Problem**: To render underwater caustics, god rays, or depth fog, need scene depth texture.

**Reference** (original water system): Uses reflection/refraction RTT but doesn't capture depth.

**Impact**: Can't implement:
- Underwater fog/visibility
- Depth-based color absorption (Issue #4)
- Shore fade/foam (depth-dependent)

**Fix Required**: Add depth texture capture in water manager:
```cpp
// In WaterManager or OceanWaterRenderer
osg::ref_ptr<osg::Camera> mDepthCamera;
osg::ref_ptr<osg::Texture2D> mDepthTexture;

// Configure depth camera to render scene depth to texture
// Bind to shader as uDepthTexture
```

---

### 13. ⚠️ Subdivision Tracker Never Used Effectively

**Location**: `apps/openmw/mwrender/oceanwaterrenderer.cpp:89-90`

**Problem**: Subdivision tracker is updated but LOD transitions aren't smooth.

**Current Code**:
```cpp
mSubdivisionTracker.update(osg::Vec2f(playerPos.x(), playerPos.y()));
```

**Issue**: No hysteresis or smoothing. As player moves, chunks pop in/out suddenly.

**Reference**: Terrain system uses "trail" effect - higher subdivision persists briefly after player leaves area.

**Fix Required**: Implement LOD smoothing similar to terrain:
```cpp
// In subdivision tracker
float mTrailTime = 2.0f;  // Keep high LOD for 2 seconds after player leaves
std::map<ChunkKey, float> mSubdivisionTimers;
```

---

### 14. ⚠️ Missing Shader Compilation Error Handling

**Location**: `components/ocean/oceanfftsimulation.cpp:121-160` (`loadShaderPrograms()`)

**Problem**: If shader loading fails, returns false but doesn't provide details.

**Current Code**:
```cpp
if (!spectrumShader || !butterflyShader || !displacementShader)
{
    Log(Debug::Error) << "Failed to load ocean FFT compute shaders";
    return false;  // ❌ No details on WHICH shader failed or WHY
}
```

**Impact**: Debugging shader issues is difficult.

**Fix Required**:
```cpp
if (!spectrumShader) {
    Log(Debug::Error) << "Failed to load spectrum shader: core/ocean/fft_update_spectrum.comp";
    Log(Debug::Error) << "Shader error: " << shaderManager.getShaderCompileError();
    return false;
}
// ... repeat for each shader
```

---

## Minor Issues (Low Priority)

### 15. ℹ️ Hard-coded Shader Paths

**Location**: `oceanfftsimulation.cpp:132-137`, `oceanwaterrenderer.cpp:275-278`

**Problem**: Shader paths are hard-coded strings, not configurable.

**Impact**: Modders can't easily replace ocean shaders.

**Fix**: Use VFS path resolution or configuration file.

---

### 16. ℹ️ Fixed Seed for Spectrum Generation

**Location**: `oceanfftsimulation.cpp:430`

**Problem**:
```cpp
std::mt19937 gen(42); // Fixed seed for reproducibility
```

**Impact**: Every ocean in the world has **identical wave patterns**.

**Fix**: Seed based on world cell coordinates:
```cpp
std::mt19937 gen(cellX * 1000 + cellY);  // Different per region
```

---

### 17. ℹ️ Memory Barriers May Be Excessive

**Location**: `oceanfftsimulation.cpp:205, 224, 239, 253`

**Problem**: Memory barrier after every compute dispatch may hurt performance.

**Current**:
```cpp
ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);  // Every dispatch
```

**Optimization**: Only need barriers between dependent passes:
```cpp
// After spectrum update - needed for FFT input
ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

// After all FFT passes - NOT needed between stages (can pipeline)
// After displacement generation - needed for vertex shader
ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
```

---

### 18. ℹ️ No Performance Metrics/Profiling

**Location**: Entire system

**Problem**: No timing or performance measurement.

**Impact**: Can't identify bottlenecks or optimize.

**Fix**: Add osg::Timer probes:
```cpp
osg::Timer_t start = osg::Timer::instance()->tick();
dispatchCompute(state);
osg::Timer_t end = osg::Timer::instance()->tick();
double ms = osg::Timer::instance()->delta_m(start, end);
Log(Debug::Verbose) << "FFT compute took " << ms << "ms";
```

---

## Missing Features (vs. Reference)

### 19. 📋 No Adaptive Filtering (Aliasing Mitigation)

**Reference** (GodotOceanWaves): Switches between bicubic and bilinear sampling based on pixel/texel ratio.

**Impact**: Distant ocean may have aliasing artifacts (shimmering).

**Fix**: Implement mipmap-based filtering or adaptive sampling in vertex shader.

---

### 20. 📋 No Sea Spray Particles

**Reference**: GPU particles for spray at wave peaks.

**Impact**: Missing visual detail at wave crests.

**Fix**: Emit particles from high-foam areas (future enhancement).

---

### 21. 📋 No Cascade Update Staggering (Implemented But Not Effective)

**Location**: `oceanfftsimulation.cpp:265-275` (`update()`)

**Problem**: Code exists to stagger updates but breaks after first cascade (line 273):
```cpp
break;  // Only update one cascade per frame for performance
```

**Issue**: With 3 cascades, takes 3 frames to fully update all → temporal lag.

**Better Approach**: Update all cascades but at different rates:
```cpp
cascade[0].updateInterval = 0.033f;  // 30 FPS
cascade[1].updateInterval = 0.050f;  // 20 FPS
cascade[2].updateInterval = 0.100f;  // 10 FPS (largest, slowest changing)
```

---

## Architecture Strengths ✅

Despite the issues, the architecture has solid foundations:

1. ✅ **Clean Separation**: Water type classification, FFT simulation, and rendering are modular
2. ✅ **Dual System**: Legacy and ocean renderers coexist gracefully
3. ✅ **Cascade System**: Multi-scale approach is correct (matches reference)
4. ✅ **Subdivision**: Character-centered LOD is implemented
5. ✅ **Compute Shader Detection**: Graceful fallback if unsupported
6. ✅ **Spectrum Generation**: Phillips spectrum implementation is mathematically correct
7. ✅ **Butterfly Texture**: FFT precomputation is correct

---

## Comparison with Reference Implementation

| Feature | GodotOceanWaves | OpenMW Ocean | Status |
|---------|----------------|--------------|--------|
| **FFT Algorithm** | Stockham | Cooley-Tukey | ✅ Implemented (different but valid) |
| **Spectrum** | TMA + JONSWAP | Phillips | ⚠️ Simpler but functional |
| **Cascades** | 3-4 scales | 3 scales | ✅ Implemented |
| **Displacement** | XYZ + Choppiness | XYZ + Choppiness | ⚠️ Implemented but uniforms not set |
| **Normals** | Derivative-based | Derivative-based | ✅ Implemented |
| **Foam** | Jacobian + persistence | Jacobian only | ⚠️ No persistence (Issue #8) |
| **Blending** | Bicubic/Bilinear | None | ❌ Missing (Issue #19) |
| **Material** | Full PBR BSDF | Basic Phong | ❌ Too simple (Issues #3, #4) |
| **Reflection** | Fresnel-based | Basic Fresnel | ❌ No RTT (Issue #3) |
| **Absorption** | Beer-Lambert | None | ❌ Missing (Issue #4) |
| **Spray Particles** | GPU particles | None | ❌ Missing (Issue #20) |

---

## Recommended Fix Priority

### Phase 1: Make It Work (Blocking Issues)
1. **Issue #1**: Set compute shader uniforms ← **CRITICAL**
2. **Issue #5**: Initialize cascade tile sizes early
3. **Issue #10**: Set choppiness uniform
4. **Issue #6**: Reduce wave amplitude to realistic values

### Phase 2: Make It Look Right (Visual Quality)
5. **Issue #2**: Enable alpha blending
6. **Issue #3**: Add reflection/refraction RTT
7. **Issue #4**: Implement depth-based absorption
8. **Issue #8**: Add foam persistence

### Phase 3: Polish (Details)
9. **Issue #7**: Fix butterfly texture binding
10. **Issue #9**: Verify normal space transformations
11. **Issues #11-14**: Refinements and error handling

### Phase 4: Optimize (Performance)
12. **Issue #17**: Reduce unnecessary barriers
13. **Issue #18**: Add profiling
14. **Issue #21**: Improve cascade update strategy

---

## Conclusion

The ocean system has **excellent architecture** but is **completely non-functional** due to:
1. Compute shaders executing with uninitialized uniforms (garbage output)
2. Missing rendering state (no blending, no transparency)
3. Overly simplified visual material (no reflections, absorption, or depth)

**Estimated Effort**:
- Phase 1 (Make It Work): **4-8 hours**
- Phase 2 (Make It Look Right): **16-24 hours**
- Phase 3 (Polish): **8-12 hours**
- Phase 4 (Optimize): **4-8 hours**

**Total**: ~2-3 days of focused development to reach MVP quality.

---

## Testing Checklist

Once fixes are applied, verify:

- [ ] Ocean chunks render at sea cells (Seyda Neen coast)
- [ ] Waves visibly animate (not static)
- [ ] Wave normals create lighting variation
- [ ] Foam appears at wave crests
- [ ] Water is transparent (can see underwater)
- [ ] Reflections of sky/environment visible
- [ ] Water color shifts with depth (shallow vs deep)
- [ ] No visual artifacts (flickering, seams, aliasing)
- [ ] Performance acceptable (>30 FPS on mid-range GPU)
- [ ] Legacy water still works for lakes/ponds
- [ ] Graceful fallback without compute shader support

---

**End of Audit Report**
