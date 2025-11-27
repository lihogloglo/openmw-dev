# Lake System Master Tracker

# Goal
Comprehensive overhaul and stabilization of the Multi-Level Lake System. This document tracks all implementation steps, test results, and architectural improvements.

## Status Overview
- **Current Phase**: Session 2 - Coordinate Space Analysis & Fixes
- **Last Action**: Fixed coordinate space separation, increased SSR thickness, initialized fallback cubemap
- **Next Step**: Debug lake movement issue and investigate blue color

## Completed Tasks

### Phase 1: Audit & Critical Fixes (Session 1)
- [x] **System Audit**: Analyzed `WaterManager`, `Lake`, `CubemapReflectionManager`, and shaders.
    -   *Findings*: SSR distance was too short (2ft), fallback cubemap was uninitialized (black), `ssrmanager.cpp` missing.
    -   *Reference*: [Lake_System_Audit.md](file:///d:/Gamedev/OpenMW/openmw-dev-waterlevels/Lake_System_Audit.md)
- [x] **Fix Black Water Fallback**:
    -   *Action*: Initialized `mFallbackCubemap` in `cubemapreflection.cpp` with a static "Sky Blue" color.
    -   *Result*: Water no longer turns pitch black when outside a reflection region.
- [x] **Fix Surface Jitter**:
    -   *Action*: Added `-5.0` bias to IOR check in `lake.frag`.
    -   *Result*: Prevents IOR flipping/flickering at water surface level.
- [x] **Fix Altitude Color/IOR Issues**:
    -   *Action*: Updated `lake.cpp` to pass true world camera position (from Inverse View Matrix) to `cameraPos` uniform. Updated `lake.frag` to use this uniform for IOR check.
    -   *Result*: **VERIFIED**. User confirmed lakes don't change color with altitude.

### Phase 2: Coordinate Space & Reflection Fixes (Session 2)
- [x] **Coordinate Space Analysis**:
    -   *Action*: Analyzed Godot SSR reference shader (`godotssr/shaders/water.gdshader`) to determine correct coordinate spaces
    -   *Finding*: **SSR must use VIEW SPACE, Cubemaps must use WORLD SPACE**
    -   *Reference*: Godot uses view space for SSR raymarching (lines 132-141 in water.gdshader)
- [x] **Fix Coordinate Space Separation** (`lake.frag` lines 135-158):
    -   *Action*: Properly separated view space (SSR) from world space (cubemap/fresnel)
    -   Created `worldViewDir = normalize(worldPos - cameraPos)` for cubemap reflections
    -   Kept `viewDir = normalize(position.xyz)` for SSR raymarching
    -   *Result*: **PARTIAL SUCCESS** - Reflections now visible, but source unclear (SSR vs cubemap?)
- [x] **Fix SSR Thickness**:
    -   *Action*: Increased `SSR_THICKNESS` from `0.5` to `500.0` in `lake.frag` line 41
    -   *Reason*: 0.5 units was impossibly small for Morrowind's scale (22.1 units = 1 foot)
    -   *Result*: SSR should now detect hits properly
- [x] **Improve SSR Raymarching**:
    -   *Action*: Normalized `reflectDir`, added detailed comments in `traceSSR()` function
    -   *Result*: Algorithm now matches Godot reference more closely
- [x] **Initialize Fallback Cubemap** (`cubemapreflection.cpp` lines 62-79):
    -   *Action*: Initialize all 6 faces with sky blue (RGB: 135, 206, 235) instead of leaving uninitialized
    -   *Result*: Fallback cubemap no longer renders black

## Known Issues (Needs Investigation)

### Session 2 Test Results
- [~] **Reflections Now Visible**:
    -   *Status*: **PARTIAL SUCCESS** - User reports seeing reflections!
    -   *Unknown*: Source unclear - is it SSR or cubemaps? Use debug modes 4-6 to determine.
- [ ] **Lake Movement Persists**:
    -   *Status*: **STILL BROKEN** - Lakes continue to move when camera/character moves
    -   *Hypothesis*: Likely related to world-space calculations, not coordinate space issue
    -   *Next Step*: Investigate geometry transformation or normal calculations
- [ ] **Lakes Still Very Blue**:
    -   *Status*: **NEW ISSUE** - Water appears overly blue/turquoise
    -   *Possible Causes*:
        1. Fallback cubemap is sky blue (RGB: 135, 206, 235) - might be dominating
        2. `WATER_COLOR` constant in shader is dark (line 34: `vec3(0.090195, 0.115685, 0.12745)`)
        3. Fresnel mixing might be off
        4. SSR confidence might be too low, letting cubemap dominate
    -   *Next Step*: Use debug mode 5 to check if cubemap is the source of blue color

## Backlog / Future Work

### Phase 3: Architecture Refactor
- [ ] **Create `SSRManager`**:
    -   Extract SSR logic from `lake.frag` and `LakeStateSetUpdater` into a dedicated C++ class/shader pair for better maintainability.
    -   *Context*: User expected `ssrmanager.cpp` to exist.
- [ ] **Dynamic Fallback Cubemap**:
    -   Implement real-time (or low frequency) sky capture for the fallback cubemap instead of static blue.
    -   *Benefit*: Better visual integration with changing weather/time of day.

### Phase 4: Optimization & Polish
- [ ] **Performance Tuning**:
    -   Evaluate cost of `SSR_MAX_STEPS` (32) vs `SSR_MAX_DISTANCE` (8192).
    -   Consider hierarchical raymarching or downsampled depth buffer if performance drops.
- [ ] **Water Edge Blending**:
    -   Investigate "hard edge" artifacts where water meets terrain (depth feathering).

## Technical Notes & Decisions
- **SSR Distance**: Set to 8192.0 (1 cell) to match Morrowind's scale. 50.0 was likely a copy-paste error from a different unit system (meters vs MW units).
- **SSR Thickness**: Set to 500.0 (was 0.5). At Morrowind's scale (22.1 units = 1 foot), 0.5 units ≈ 0.27 inches - far too small for ray-hit detection.
- **IOR Bias**: The `-5.0` bias is a heuristic. If jitter persists, we may need a more robust "is camera underwater" uniform passed from C++.
- **Fallback Color**: RGB(135, 206, 235) is a placeholder sky blue. Ideally, this should sample the current fog/sky color uniform.
- **Coordinate Spaces** (Session 2 analysis):
    - **SSR**: MUST use view space (camera-relative) - confirmed by Godot reference shader
    - **Cubemap**: MUST use world space (absolute directions) - standard cubemap sampling
    - **Fresnel**: Uses world space view direction for physically accurate results
    - Mixing these spaces was causing previous failures

## Debug Commands
Use these in-game to diagnose issues:
- **debugMode = 4**: SSR only (white/colored = SSR hit, black = no SSR)
- **debugMode = 5**: Cubemap only (shows pure cubemap reflection)
- **debugMode = 6**: SSR confidence (white = high SSR confidence, black = using cubemap)
- **debugMode = 9**: Water color tinted (base water color without reflections)

## Session 2 Summary
**What We Fixed:**
1. Separated coordinate spaces correctly (view for SSR, world for cubemap)
2. Increased SSR thickness by 100x (0.5 → 500.0)
3. Initialized fallback cubemap with sky blue instead of black
4. Normalized reflection direction in SSR raymarching

**What We Learned:**
- Godot reference confirms view space for SSR is correct
- Cubemap generation code exists and should work
- Coordinate space mixing was likely causing invisible reflections

**What Still Needs Fixing:**
- Lake movement when camera/character moves (geometry issue?)
- Overly blue water color (investigate cubemap vs water color mixing)
- Determine if SSR or cubemaps are producing the visible reflections
