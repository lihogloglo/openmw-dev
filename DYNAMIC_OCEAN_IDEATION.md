# Dynamic Ocean System - Ideation Document

## Executive Summary

This document proposes an implementation strategy for a dynamic ocean wave system in OpenMW, leveraging the existing character-centered terrain subdivision system. The approach addresses the unique challenges of Morrowind's water system while drawing inspiration from GodotOceanWaves' FFT-based implementation.

---

## Table of Contents

1. [Problem Analysis](#problem-analysis)
2. [Current System Overview](#current-system-overview)
3. [Proposed Architecture](#proposed-architecture)
4. [Implementation Approaches](#implementation-approaches)
5. [Technical Deep Dives](#technical-deep-dives)
6. [Performance Considerations](#performance-considerations)
7. [Phased Implementation Plan](#phased-implementation-plan)
8. [Alternative Approaches](#alternative-approaches)

---

## 1. Problem Analysis

### Challenges Identified

1. **Single Water Plane Limitation**
   - Current: One global water geometry (`150 * 8192` units, 40x40 segments)
   - Problem: Ocean waves shouldn't apply to ponds, rivers, or indoor water
   - Impact: Need water type classification and selective wave application

2. **Shore Transition Requirements**
   - Current: No wave damping near shorelines
   - Problem: Waves at full amplitude on beaches would look unrealistic
   - Impact: Requires distance-to-shore computation and wave attenuation

3. **Indoor vs Outdoor Water**
   - Current: Different positioning logic but same rendering
   - Problem: Indoor water should be completely flat
   - Impact: Need conditional wave application based on cell type

4. **Performance Constraints**
   - Current: Simple vertex shader with 6 scrolling normal maps
   - Problem: FFT-based waves are computationally expensive
   - Impact: Must balance visual quality with frame rate

### Opportunities

1. **Existing Subdivision System** - Character-centered LOD already implemented for terrain
2. **Shader Infrastructure** - Advanced water shaders with normal mapping and Fresnel
3. **Cell System** - Natural boundaries for water type classification
4. **RTT Pipeline** - Reflection/refraction already using render-to-texture

---

## 2. Current System Overview

### Water Rendering Pipeline (Simplified)

```
Water::Water() initialization
    ↓
createWaterGeometry(size, segments, repeats)
    ↓ (produces single 40x40 quad mesh)
WaterNode (PositionAttitudeTransform)
    ├── WaterGeometry (single mesh, ~1600 quads)
    ├── Reflection RTT Camera
    ├── Refraction RTT Camera
    └── Ripples RTT Camera (1024x1024 GPU simulation)
        ↓
water.frag shader
    ├── 6 scrolling normal map layers
    ├── Fresnel reflection/refraction blend
    ├── Rain ripple integration
    └── Specular highlights
```

### Key Constraints

- **Fixed Geometry**: Single mesh created once, positioned per cell
- **Shader-Based Animation**: All motion via texture scrolling, no vertex displacement
- **Cell-Based Height**: Each cell has one water level (`float mWater`)
- **No Depth Variation**: Constant depth assumption throughout

---

## 3. Proposed Architecture

### Water Type Classification System

Introduce a **WaterType** enum to categorize water bodies:

```cpp
enum class WaterType {
    OCEAN,          // Large exterior water, full wave simulation
    LARGE_LAKE,     // Large bodies (>10 cells²), reduced waves
    SMALL_LAKE,     // Medium ponds (1-10 cells²), minimal waves
    POND,           // Tiny water (<1 cell²), no waves
    RIVER,          // Flowing water, current-aligned waves
    INDOOR          // Interior cells, completely flat
};
```

### Multi-Level Water Geometry System

**Replace single water plane with hierarchical water system:**

```
WaterManager
├── OceanWaterRenderer (FFT-based, character-centered subdivision)
│   ├── Near water chunks (< 512m): High subdivision + full FFT
│   ├── Mid water chunks (512-2048m): Medium subdivision + simplified waves
│   └── Far water chunks (> 2048m): Low subdivision + shader-only animation
│
├── LakeWaterRenderer (simplified Gerstner waves)
│   └── Per-lake instances with distance-based LOD
│
└── StaticWaterRenderer (current system)
    └── Ponds, rivers, indoor water
```

---

## 4. Implementation Approaches

### Approach A: FFT-Based Ocean Waves (GodotOceanWaves Style)

**Technique:** Fast Fourier Transform of directional ocean-wave spectra

#### Architecture

```cpp
class OceanFFTSimulation {
    // Spectral domain
    std::vector<osg::ref_ptr<osg::Texture2D>> mSpectrumTextures;  // H(k) initial spectrum
    std::vector<osg::ref_ptr<osg::Texture2D>> mDisplacementMaps;  // xyz displacement
    std::vector<osg::ref_ptr<osg::Texture2D>> mNormalMaps;        // Normal vectors
    std::vector<osg::ref_ptr<osg::Texture2D>> mFoamMaps;          // Jacobian-based foam

    // FFT compute pipeline
    osg::ref_ptr<osg::Program> mFFTComputeShader;
    osg::ref_ptr<osg::Program> mSpectrumUpdateShader;

    // Wave parameters
    float mWindSpeed;           // m/s
    float mFetchDistance;       // meters
    float mWaterDepth;          // meters
    osg::Vec2f mWindDirection;  // normalized

    // Cascade system (multi-scale)
    struct WaveCascade {
        float tileSize;         // World-space size
        int textureResolution;  // e.g., 256, 512, 1024
        float updateInterval;   // Seconds between updates
        float timeSinceUpdate;
    };
    std::vector<WaveCascade> mCascades;
};
```

#### Shader Pipeline

**1. Spectrum Generation (Initial, CPU or Compute)**
```glsl
// TMA spectrum with JONSWAP formulation
float TMA_Spectrum(float omega, float theta, float U, float D, float F) {
    float omega_p = calculatePeakFrequency(U, F);  // Peak frequency
    float alpha = calculatePhillipsAlpha(U, F);    // Energy scale

    // JONSWAP spectrum
    float jonswap = alpha * g^2 / omega^5 *
                    exp(-1.25 * (omega_p/omega)^4) *
                    gamma^(exp(-(omega-omega_p)^2 / (2*sigma^2*omega_p^2)));

    // Depth attenuation (TMA modification)
    float depth_atten = tanh(omega^2 * D / g);

    // Directional spreading
    float spreading = HasselmannSpread(theta, omega, omega_p);

    return jonswap * depth_atten * spreading;
}
```

**2. FFT Computation (Compute Shader)**
```glsl
// Stockham FFT algorithm (avoids bit-reversal)
layout(local_size_x = 16, local_size_y = 16) in;

uniform image2D uInputTexture;
uniform image2D uOutputTexture;
uniform sampler2D uButterflyTexture;  // Pre-computed twiddle factors

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

    // Horizontal FFT pass
    vec4 H0k = imageLoad(uInputTexture, pos);
    vec2 twiddle = texelFetch(uButterflyTexture, pos, 0).rg;

    // Butterfly operation
    vec4 result = butterflyOperation(H0k, twiddle);
    imageStore(uOutputTexture, pos, result);
}
```

**3. Displacement Application (Vertex Shader)**
```glsl
in vec3 aPosition;
in vec2 aTexCoord;

out vec3 vWorldPos;
out vec3 vDisplacedPos;

uniform sampler2D uDisplacementMap;  // xyz displacement
uniform float uChoppiness;           // Wave steepness control

void main() {
    vec3 displacement = texture(uDisplacementMap, aTexCoord).rgb;

    // Apply displacement with choppiness scaling
    vec3 displacedPos = aPosition;
    displacedPos.xy += displacement.xy * uChoppiness;  // Horizontal
    displacedPos.z += displacement.z;                   // Vertical

    vWorldPos = aPosition;
    vDisplacedPos = displacedPos;
    gl_Position = uViewProjection * vec4(displacedPos, 1.0);
}
```

#### Pros
- Physically accurate ocean appearance
- Scalable performance via FFT (O(N log N))
- Natural foam generation via Jacobian
- Multiple wave scales via cascades

#### Cons
- High GPU compute requirement (need compute shaders)
- Complex implementation and debugging
- May not work on older hardware (no compute shader support)
- Overkill for small lakes/ponds

---

### Approach B: Simplified Gerstner Waves

**Technique:** Sum of sinusoidal wave functions with circular motion

#### Architecture

```cpp
class GerstnerWaveSimulation {
    struct Wave {
        float wavelength;     // Lambda (meters)
        float amplitude;      // A (meters)
        float speed;          // c (m/s)
        float steepness;      // Q (0-1, controls breaking)
        osg::Vec2f direction; // Normalized direction
        float phase;          // Initial offset
    };

    std::vector<Wave> mWaves;  // 4-8 waves typically sufficient
    float mTime;                // Accumulated time for phase
};
```

#### Shader Implementation

```glsl
struct Wave {
    vec2 direction;
    float wavelength;
    float amplitude;
    float speed;
    float steepness;
};

uniform Wave uWaves[8];
uniform int uWaveCount;
uniform float uTime;

vec3 GerstnerWave(vec2 pos, Wave w, float time) {
    float k = 2.0 * PI / w.wavelength;  // Wave number
    float c = w.speed;
    float a = w.amplitude;
    float Q = w.steepness;

    vec2 d = normalize(w.direction);
    float f = k * dot(d, pos) - c * time;

    // Gerstner displacement
    vec3 displacement;
    displacement.x = Q * a * d.x * cos(f);
    displacement.y = Q * a * d.y * cos(f);
    displacement.z = a * sin(f);

    return displacement;
}

void main() {
    vec3 totalDisplacement = vec3(0.0);

    for (int i = 0; i < uWaveCount; i++) {
        totalDisplacement += GerstnerWave(aPosition.xy, uWaves[i], uTime);
    }

    vec3 displacedPos = aPosition + totalDisplacement;
    gl_Position = uViewProjection * vec4(displacedPos, 1.0);
}
```

#### Pros
- Simple to implement and understand
- Runs on any GPU with vertex shaders
- Controllable wave parameters
- Good for medium-sized lakes

#### Cons
- Less realistic than FFT for open ocean
- O(N) complexity per wave
- Limited to ~8 waves before performance degrades
- No natural foam generation

---

### Approach C: Character-Centered Subdivision + Displacement Textures

**Technique:** Adapt terrain subdivision system for water meshes

#### Architecture

```cpp
class SubdividedWaterGeometry {
    // Reuse terrain subdivision components
    SubdivisionTracker mSubdivisionTracker;

    struct WaterChunk {
        osg::ref_ptr<osg::Geometry> geometry;
        int subdivisionLevel;           // 0-3
        osg::Vec2f chunkCenter;
        WaterType waterType;
        float distanceToNearestShore;   // For wave attenuation
    };

    std::map<osg::Vec2i, WaterChunk> mWaterChunks;

    // Displacement source (could be FFT or pre-baked)
    osg::ref_ptr<osg::Texture2D> mDisplacementTexture;
    osg::ref_ptr<osg::Texture2D> mNormalTexture;
};
```

#### Subdivision Logic (Adapted from Terrain)

```cpp
int getWaterSubdivisionLevel(const osg::Vec2f& chunkCenter,
                              const osg::Vec2f& playerPos,
                              WaterType type) {
    if (type == WaterType::INDOOR || type == WaterType::POND) {
        return 0;  // No subdivision for static water
    }

    float distance = (chunkCenter - playerPos).length();

    // Distance thresholds for ocean water
    if (type == WaterType::OCEAN) {
        if (distance < 512.0f) return 3;   // 256x triangles, full FFT
        if (distance < 1536.0f) return 2;  // 64x triangles, simplified
        if (distance < 4096.0f) return 1;  // 16x triangles, shader-only
        return 0;  // Base mesh, simple animation
    }

    // Lakes use less subdivision
    if (type == WaterType::LARGE_LAKE) {
        if (distance < 768.0f) return 2;
        if (distance < 2048.0f) return 1;
        return 0;
    }

    return 0;  // Default: no subdivision
}
```

#### Pros
- Reuses proven subdivision system
- Character-centered detail distribution
- Trail effect for smooth transitions
- Performance scales with player proximity

#### Cons
- More complex geometry management
- Higher vertex throughput than single mesh
- Requires chunk boundary blending

---

## 5. Technical Deep Dives

### 5.1 Water Type Detection

**Method 1: Cell Metadata Extension**

Extend `ESM::Cell` structure:

```cpp
// In components/esm3/loadcell.hpp
struct Cell {
    float mWater;
    bool mHasWaterHeightSub;
    WaterType mWaterType;  // NEW: Water classification

    // NEW: Ocean-specific parameters
    struct OceanParams {
        float windSpeed;
        osg::Vec2f windDirection;
        float fetchDistance;
        float depth;
    } mOceanParams;
};
```

**Method 2: Automatic Detection via World Data**

```cpp
WaterType detectWaterType(const MWWorld::CellStore* cell) {
    const ESM::Cell* cellData = cell->getCell();

    // Interior cells always static
    if (!cellData->isExterior()) {
        return WaterType::INDOOR;
    }

    // Analyze connected water cells
    int waterCellCount = countConnectedWaterCells(cell);

    // Check if connected to ocean (edge of world)
    bool isOcean = isConnectedToWorldEdge(cell);

    if (isOcean) return WaterType::OCEAN;
    if (waterCellCount > 100) return WaterType::LARGE_LAKE;
    if (waterCellCount > 10) return WaterType::SMALL_LAKE;
    return WaterType::POND;
}
```

**Method 3: Texture-Based Classification**

Use existing cell terrain textures as hints:
- `Tx_water_01.dds` = Ocean
- `Tx_water_lake.dds` = Lake
- Indoor cells = Indoor water

### 5.2 Shore Distance Computation

**Challenge:** Need per-vertex distance to nearest shore for wave attenuation

**Solution A: Pre-Computed Distance Field**

```cpp
class ShoreDistanceField {
    // 2D texture storing distance to shore
    osg::ref_ptr<osg::Texture2D> mDistanceField;

    void generateDistanceField(const std::vector<osg::Vec2f>& shoreline) {
        // Jump flooding algorithm (GPU-accelerated SDF)
        // 1. Initialize: shore pixels = 0, water pixels = MAX
        // 2. Jump flood passes: progressively smaller jumps
        // 3. Result: each water pixel stores distance to nearest shore
    }
};
```

**Shader Usage:**
```glsl
uniform sampler2D uShoreDistanceField;
uniform float uShoreTransitionWidth;  // e.g., 100 meters

void main() {
    float distToShore = texture(uShoreDistanceField, worldPosXY / worldSize).r;

    // Attenuation factor: 0 at shore, 1 in deep water
    float waveStrength = smoothstep(0.0, uShoreTransitionWidth, distToShore);

    vec3 displacement = computeWaveDisplacement(...) * waveStrength;
    // ...
}
```

**Solution B: Runtime Raycast**

```cpp
float getDistanceToShore(const osg::Vec2f& waterPos) {
    const float MAX_SEARCH_DISTANCE = 512.0f;
    const int NUM_RAYS = 16;

    float minDist = MAX_SEARCH_DISTANCE;

    for (int i = 0; i < NUM_RAYS; i++) {
        float angle = (i / float(NUM_RAYS)) * 2.0 * PI;
        osg::Vec2f dir(cos(angle), sin(angle));

        float dist = raycastToShore(waterPos, dir, MAX_SEARCH_DISTANCE);
        minDist = std::min(minDist, dist);
    }

    return minDist;
}
```

Pros: Accurate, no pre-computation
Cons: Expensive, best cached per chunk

**Recommended: Hybrid Approach**
- Pre-compute distance field at chunk creation time
- Store as vertex attribute or lookup texture
- Update when terrain changes (rare)

### 5.3 FFT Implementation Details

**Butterfly Texture Generation**

```glsl
// Pre-compute once per resolution
vec4 generateButterflyTexture(ivec2 coord, int N) {
    int stage = coord.y;
    int index = coord.x;

    int butterfly_span = 1 << stage;
    int butterfly_width = butterfly_span << 1;

    int butterfly_index = index & (butterfly_span - 1);
    int twiddle_index = butterfly_index * N / butterfly_width;

    // Complex twiddle factor: e^(-2πi * k/N)
    float angle = -2.0 * PI * float(twiddle_index) / float(N);
    vec2 twiddle = vec2(cos(angle), sin(angle));

    int top_wing = (index / butterfly_width) * butterfly_width + butterfly_index;
    int bottom_wing = top_wing + butterfly_span;

    return vec4(twiddle, float(top_wing), float(bottom_wing));
}
```

**Update Strategy**

```cpp
void OceanFFTSimulation::update(float dt) {
    // Only update cascades when needed (staggered)
    for (auto& cascade : mCascades) {
        cascade.timeSinceUpdate += dt;

        if (cascade.timeSinceUpdate >= cascade.updateInterval) {
            updateCascade(cascade);
            cascade.timeSinceUpdate = 0.0f;
            break;  // Only one cascade per frame (load balancing)
        }
    }
}

void updateCascade(WaveCascade& cascade) {
    // 1. Update spectrum in frequency domain
    dispatchCompute(mSpectrumUpdateShader, cascade.textureResolution);

    // 2. Horizontal FFT pass
    dispatchComputeFFT(CASCADE_DIRECTION_HORIZONTAL, cascade);

    // 3. Transpose
    dispatchCompute(mTransposeShader, cascade.textureResolution);

    // 4. Vertical FFT pass
    dispatchComputeFFT(CASCADE_DIRECTION_VERTICAL, cascade);

    // 5. Generate normals and foam
    dispatchCompute(mDerivativeShader, cascade.textureResolution);
}
```

### 5.4 Integration with Existing Systems

**Ripple System Integration**

```cpp
// In water.cpp
void Water::update(float dt, bool paused) {
    // Existing ripple simulation
    if (mSimulation && !paused) {
        mSimulation->update(dt);
    }

    // NEW: Ocean wave simulation
    if (mOceanSimulation && !paused && isOceanCell()) {
        mOceanSimulation->update(dt);
    }

    // Blend ripples with ocean waves in shader
}
```

**Shader Blending**
```glsl
// In water.frag
void main() {
    // Existing: Ripple normals
    vec3 rippleNormal = texture(uRippleTexture, ...).rgb;

    // NEW: Ocean wave normals
    vec3 oceanNormal = texture(uOceanNormalMap, ...).rgb;

    // Blend based on water type and distance
    float oceanWeight = getOceanWeight();  // 0 for ponds, 1 for ocean
    vec3 finalNormal = mix(rippleNormal, oceanNormal, oceanWeight);

    // Rest of existing shader...
}
```

**Reflection/Refraction Compatibility**

Ocean waves require updated clip planes:

```cpp
void Reflection::setClipPlane(float waterHeight, const osg::Vec3f& waveDisplacement) {
    // Account for wave displacement in clip plane calculation
    float maxWaveHeight = getMaxWaveAmplitude();
    mClipPlane = osg::Plane(
        osg::Vec3f(0, 0, 1),  // Normal (up)
        osg::Vec3f(0, 0, waterHeight + maxWaveHeight)  // Point on plane
    );
}
```

---

## 6. Performance Considerations

### 6.1 Compute Shader Fallback

For systems without compute shader support:

```cpp
bool OceanFFTSimulation::init() {
    if (supportsComputeShaders()) {
        initComputePipeline();
    } else {
        // Fallback: Pre-baked displacement animations
        loadPrecomputedDisplacementSequence();
        // or fall back to Gerstner waves
        mFallbackGerstner = std::make_unique<GerstnerWaveSimulation>();
    }
}
```

### 6.2 Memory Budget

**FFT Textures (per cascade):**
- Displacement map: 1024² × RGBA16F = 8 MB
- Normal map: 1024² × RGBA8 = 4 MB
- Foam map: 1024² × R8 = 1 MB
- **Total per cascade: ~13 MB**

**3 cascades:** ~40 MB (acceptable for modern GPUs)

### 6.3 Update Frequency Tuning

```cpp
struct PerformancePreset {
    int cascadeCount;
    int resolution;
    float updateInterval;
};

const PerformancePreset PRESETS[] = {
    // Ultra: 3 cascades, 1024², 0.033s (30 FPS updates)
    {3, 1024, 0.033f},

    // High: 3 cascades, 512², 0.050s (20 FPS updates)
    {3, 512, 0.050f},

    // Medium: 2 cascades, 512², 0.100s (10 FPS updates)
    {2, 512, 0.100f},

    // Low: Gerstner fallback
    {0, 0, 0.0f}
};
```

### 6.4 Subdivision Performance

Based on terrain subdivision system:

**Vertex count estimation:**
- Base ocean chunk: 10×10 quads = 100 quads = 400 vertices
- Level 1 subdivision: 400 × 4 = 1,600 vertices
- Level 2 subdivision: 400 × 16 = 6,400 vertices
- Level 3 subdivision: 400 × 64 = 25,600 vertices

**Visible chunks:** ~20-30 chunks in character-centered system
**Total vertices (level 2):** 30 × 6,400 = 192,000 vertices (manageable)

---

## 7. Phased Implementation Plan

### Phase 1: Foundation (Weeks 1-2)

**Goals:**
- Water type classification system
- Multi-geometry water rendering

**Tasks:**
1. Extend `ESM::Cell` with `WaterType` enum
2. Implement automatic water type detection
3. Create `WaterManager` class to replace single `Water` instance
4. Split rendering into `OceanWaterRenderer`, `LakeWaterRenderer`, `StaticWaterRenderer`
5. Verify existing water still works for all types

**Deliverable:** Different water types render correctly, no visual changes yet

---

### Phase 2: Subdivision System (Weeks 3-4)

**Goals:**
- Character-centered water mesh subdivision
- Distance-based LOD

**Tasks:**
1. Create `WaterChunkManager` (based on terrain `ChunkManager`)
2. Implement `WaterSubdivisionTracker` (reuse terrain logic)
3. Subdivide ocean chunks based on player proximity
4. Add chunk boundary blending to prevent seams
5. Performance profiling and optimization

**Deliverable:** Ocean has higher vertex density near player, smooth LOD transitions

---

### Phase 3A: Gerstner Waves (Weeks 5-6) - **Faster Path**

**Goals:**
- Working wave displacement
- Shore attenuation

**Tasks:**
1. Implement `GerstnerWaveSimulation` class
2. Create vertex shader for Gerstner displacement
3. Implement shore distance computation (per-chunk caching)
4. Add wave parameter configuration (wind, amplitude, etc.)
5. Tune wave parameters for Morrowind aesthetic

**Deliverable:** Animated ocean waves with shore transitions

---

### Phase 3B: FFT Waves (Weeks 5-8) - **Higher Quality Path**

**Goals:**
- FFT-based ocean simulation
- Multi-cascade system

**Tasks:**
1. Implement `OceanFFTSimulation` class
2. Create compute shaders for FFT (Stockham algorithm)
3. Generate butterfly textures
4. Implement spectrum generation (TMA/JONSWAP)
5. Create cascade management system
6. Integrate with vertex shader for displacement
7. Add compute shader fallback (pre-baked or Gerstner)

**Deliverable:** Realistic FFT-based ocean waves

---

### Phase 4: Integration & Polish (Weeks 7-9 or 9-11)

**Goals:**
- Blend with existing systems
- Foam generation
- Performance optimization

**Tasks:**
1. Integrate ripples with ocean waves (shader blending)
2. Update reflection/refraction clip planes for waves
3. Implement foam generation (Jacobian for FFT, manual for Gerstner)
4. Add rain interaction (increase choppiness)
5. Performance profiling across hardware tiers
6. Add configuration settings UI

**Deliverable:** Fully integrated dynamic ocean system

---

### Phase 5: Advanced Features (Weeks 10-12 or 12-14)

**Goals:**
- Enhanced realism
- Edge cases

**Tasks:**
1. Implement depth-based wave attenuation (shallow water)
2. Add current/flow for rivers (directional bias)
3. Weather integration (storm waves)
4. Underwater view improvements (caustics, god rays)
5. Boat interaction (buoyancy, wake trails)

**Deliverable:** Production-ready ocean system

---

## 8. Alternative Approaches

### Alternative A: Hybrid Shader Displacement (No Subdivision)

**Concept:** Keep single water mesh, use displacement mapping in tessellation shader

**Pros:**
- No geometry management complexity
- Hardware tessellation handles LOD
- Simpler implementation

**Cons:**
- Requires tessellation shader support (OpenGL 4.0+)
- Limited control over tessellation pattern
- Worse performance than manual LOD on some hardware

**Verdict:** Consider if tessellation shaders become required minimum

---

### Alternative B: Pre-Baked Wave Animations

**Concept:** Pre-compute FFT animations offline, load as texture sequence

**Pros:**
- Zero runtime FFT cost
- Guaranteed quality
- Works on any hardware

**Cons:**
- Large memory footprint (100+ frames × 13 MB = 1.3 GB+)
- Repetitive patterns
- No dynamic weather response

**Verdict:** Good fallback for low-end hardware, not primary approach

---

### Alternative C: Height Field + Flow Maps

**Concept:** 2D height field texture updated each frame, flow maps for animation

**Pros:**
- Simple to understand
- Good for stylized water
- Low vertex count

**Cons:**
- Texture resolution limits detail
- Difficult to get realistic look
- Flow maps require manual authoring

**Verdict:** Better for rivers than ocean, consider for flowing water only

---

## 9. Recommended Implementation Strategy

### Recommended Path: **Phased Hybrid Approach**

1. **Phase 1-2:** Foundation + Subdivision (all implementations need this)
2. **Phase 3A:** Implement Gerstner waves first
   - Faster development
   - Proves subdivision system works
   - Provides working ocean waves sooner
   - Good fallback for FFT

3. **Phase 3B:** Add FFT as optional upgrade
   - Develop in parallel with Phase 4
   - Use Gerstner as fallback
   - Make FFT opt-in via settings

4. **Runtime Selection:**
```cpp
enum WaveSimulationMode {
    SIMPLE,     // Shader-only (current system)
    GERSTNER,   // Vertex shader Gerstner waves
    FFT         // Compute shader FFT (highest quality)
};

// Automatic selection based on hardware
WaveSimulationMode selectSimulationMode() {
    if (supportsComputeShaders() && gpuMemory > 4GB && settings.oceanQuality == ULTRA) {
        return FFT;
    }
    if (supportsVertexShaders() && settings.oceanQuality >= MEDIUM) {
        return GERSTNER;
    }
    return SIMPLE;
}
```

---

## 10. Key Technical Decisions

### Decision Matrix

| Feature | Simple | Gerstner | FFT |
|---------|--------|----------|-----|
| **Visual Quality** | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Performance** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ |
| **Implementation Time** | ✅ Done | 2 weeks | 4 weeks |
| **Hardware Requirements** | Any | VS 2.0+ | CS 4.3+ |
| **Memory Usage** | <1 MB | <10 MB | ~40 MB |
| **Realism** | Low | Medium | High |

### Final Recommendations

**For MVP (Minimum Viable Product):**
- Use Gerstner waves with character-centered subdivision
- Implement shore distance attenuation
- Support ocean and lake water types
- Keep indoor/pond water static

**For Full Release:**
- Add FFT as optional high-quality mode
- Implement all 5 water types
- Add foam generation
- Full integration with weather and ripples

**For Performance:**
- Subdivision levels: 0-3 (avoid level 4 except extreme close-up)
- FFT cascade count: 2-3 maximum
- Update interval: 50ms minimum (20 FPS for wave updates)
- Chunk count limit: 30 visible chunks maximum

---

## 11. Open Questions & Research Needed

1. **Morrowind Water Depth Data**
   - Is bathymetry (underwater terrain) available in ESM files?
   - If not, use constant depth or estimate from cell terrain

2. **Performance Target**
   - What is acceptable FPS impact? (Suggest: <5% at 60 FPS baseline)
   - Minimum supported GPU? (Suggest: GTX 660 / RX 560)

3. **Art Direction**
   - Should Morrowind ocean be realistic or stylized?
   - Reference: Calm Mediterranean vs. stormy North Atlantic?

4. **Compatibility**
   - How to handle mods that modify water?
   - Save compatibility with older OpenMW versions?

5. **Editor Support**
   - Should OpenCS visualize wave simulation?
   - Tools for tweaking wave parameters per region?

---

## 12. References & Resources

### Papers & Algorithms
- **Tessendorf 1999:** "Simulating Ocean Water" (FFT waves foundation)
- **Hasselmann et al. 1973:** Directional wave spectra
- **Stockham 1966:** FFT algorithm without bit-reversal

### Implementations
- **GodotOceanWaves:** https://github.com/2Retr0/GodotOceanWaves/
- **Fynn-Jorin FFT Ocean:** Unity FFT ocean implementation
- **NVIDIA WaveWorks:** Commercial ocean SDK (reference only)

### OpenMW Codebase
- **Water rendering:** `apps/openmw/mwrender/water.cpp`
- **Terrain subdivision:** `components/terrain/terrainsubdivider.cpp`
- **Chunk management:** `components/terrain/chunkmanager.cpp`
- **Shaders:** `files/shaders/compatibility/water.vert/frag`

---

## Appendix A: Code Structure Outline

```
components/ocean/
├── oceanmanager.hpp/cpp           # High-level ocean system coordinator
├── watertypeclassifier.hpp/cpp    # Automatic water type detection
├── shoredistance.hpp/cpp          # Shore distance field generation
│
├── simulation/
│   ├── wavesimulation.hpp         # Abstract base class
│   ├── gerstnerwaves.hpp/cpp      # Gerstner wave implementation
│   ├── fftocean.hpp/cpp           # FFT-based simulation
│   └── cascademanager.hpp/cpp     # Multi-scale cascade system
│
├── geometry/
│   ├── waterchunkmanager.hpp/cpp  # Character-centered chunk system
│   ├── watersubdivider.hpp/cpp    # Subdivision algorithms
│   └── subdivisiontracker.hpp/cpp # LOD tracking (adapted from terrain)
│
└── rendering/
    ├── oceanrenderer.hpp/cpp      # Ocean-specific renderer
    ├── lakerenderer.hpp/cpp       # Lake renderer
    └── staticrenderer.hpp/cpp     # Pond/indoor renderer

files/shaders/ocean/
├── gerstner_vertex.glsl           # Gerstner vertex displacement
├── fft_spectrum.comp              # FFT spectrum generation
├── fft_butterfly.comp             # FFT butterfly computation
├── fft_displacement.comp          # Displacement map generation
├── ocean_vertex.glsl              # Ocean vertex shader
├── ocean_fragment.glsl            # Enhanced water fragment shader
└── shore_blend.glsl               # Shore transition utilities

apps/openmw/mwrender/
├── watermanager.hpp/cpp           # Replaces water.hpp/cpp (orchestrator)
└── [keep existing water.cpp as legacy fallback]
```

---

## Appendix B: Configuration Settings

```yaml
[Ocean]
# Simulation mode: simple, gerstner, fft
simulation_mode = gerstner

# Gerstner wave settings
gerstner_wave_count = 6
gerstner_wavelength_min = 10.0
gerstner_wavelength_max = 100.0
gerstner_steepness = 0.4
gerstner_wind_speed = 5.0

# FFT settings
fft_cascade_count = 3
fft_resolution = 512
fft_update_interval = 0.05
fft_wind_speed = 10.0
fft_fetch_distance = 100000.0
fft_choppiness = 1.5

# Subdivision settings
ocean_subdivision_max_level = 3
ocean_subdivision_near_distance = 512.0
ocean_subdivision_mid_distance = 1536.0
ocean_subdivision_far_distance = 4096.0

# Shore settings
shore_transition_width = 100.0
shore_wave_attenuation = 0.1

# Performance
max_water_chunks = 30
cascade_stagger_updates = true
```

---

## Conclusion

This ideation document proposes a comprehensive dynamic ocean system for OpenMW that:

1. **Solves the stated problems:**
   - Multiple water types (ocean, lakes, ponds, indoor)
   - Shore wave transitions via distance fields
   - Selective wave application based on water classification

2. **Leverages existing systems:**
   - Character-centered subdivision (proven with terrain/snow)
   - Shader infrastructure (normal maps, RTT, Fresnel)
   - Cell-based architecture

3. **Provides implementation flexibility:**
   - Phased approach with working results at each stage
   - Multiple simulation methods (Gerstner vs. FFT)
   - Graceful fallbacks for older hardware

4. **Maintains OpenMW design philosophy:**
   - Moddable and configurable
   - Backwards compatible
   - Performance-conscious

**Recommended next steps:**
1. Review and discuss this document with team
2. Prototype water type classification (Phase 1)
3. Test subdivision system with simple displacement (Phase 2)
4. Implement Gerstner waves as MVP (Phase 3A)
5. Evaluate FFT for future enhancement (Phase 3B)

The character-centered subdivision system provides an excellent foundation for this work, and the phased approach ensures visible progress while managing complexity.
