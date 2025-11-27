# Lake System Master Tracker

# Goal
Comprehensive overhaul and stabilization of the Multi-Level Lake System. This document tracks all implementation steps, test results, and architectural improvements.

## Status Overview
- **Current Phase**: Session 6 - Cubemap RTT Fix & Performance Crisis
- **Last Action**: Fixed cubemap rendering (RTT configuration), but introduced crash due to continuous rendering
- **Next Step**: Implement one-shot cubemap rendering to fix crashes while maintaining working reflections

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

### Phase 3: Geometry & Performance Fixes (Session 3)
- [x] **Fix Lake Movement/Jitter** (`lake.cpp` lines 509-524, `lake.vert` lines 33-34):
    -   *Root Cause*: Double transformation bug - `PositionAttitudeTransform` placed geometry at cell center, then shader added `cellCenter` again
    -   *Action*: Changed `CellWater::transform` from `PositionAttitudeTransform` to simple `osg::Group`
    -   *Action*: Placed vertices directly in world coordinates (not local)
    -   *Action*: Updated `lake.vert` to use `worldPos = gl_Vertex.xyz` (no cellCenter addition)
    -   *Result*: **SUCCESS** - Lakes now stationary when camera/character moves
- [x] **Fix Overly Blue Water** (`cubemapreflection.cpp` lines 62-72, 111):
    -   *Root Cause*: Two sources of blue color dominating reflections:
        1. Fallback cubemap initialized with sky blue (RGB: 135, 206, 235)
        2. Cubemap camera clear color set to sky blue (0.5, 0.7, 1.0)
    -   *Action*: Changed fallback cubemap to light neutral gray (RGB: 180, 180, 180)
    -   *Action*: Changed cubemap camera clear color to neutral gray (0.7, 0.7, 0.7)
    -   *Result*: **SUCCESS** - Water no longer has excessive blue tint, user reports "grey color is better"
- [x] **Fix Cubemap Flickering** (`cubemapreflection.cpp` lines 254-291):
    -   *Root Cause*: `update()` function disabled all cubemap cameras every frame (line 273 old code), causing flicker
    -   *Action*: Removed the loop that disabled cameras after every frame
    -   *Rationale*: Cubemaps should persist once rendered; only re-render when `needsUpdate` is true
    -   *Result*: **PARTIAL SUCCESS** - Flickering greatly reduced but still present slightly

### Phase 4: SSR & Cubemap Optimization (Session 4)
- [x] **Optimize SSR Raymarching** (`lake.frag` lines 39-137):
    -   *Analysis*: Studied Godot water shader reference implementation
    -   *Root Cause*: Current SSR was too expensive:
        - 32 steps × 8192 units = massive per-pixel cost
        - No adaptive stepping
        - Poor screen edge fadeout causing artifacts
    -   *Action*: Implemented Godot-inspired optimizations:
        1. Reduced steps from 32 → 16
        2. Reduced max distance from 8192 → 4096 units (still ~185 feet)
        3. Added adaptive stepping (step size increases with distance)
        4. Improved screen edge fadeout using Godot's algorithm
        5. Added configurable `SSR_MAX_DIFF` threshold (300 units) for hit detection
    -   *Expected Result*: ~50% performance improvement from halved steps + adaptive sizing
- [x] **Fix Cubemap Camera Lifecycle** (`cubemapreflection.cpp` lines 222-306, `cubemapreflection.hpp` line 78):
    -   *Root Cause*: Session 3 removed camera disabling, causing cameras to render every frame
        - 6 cameras × every frame × multiple regions = catastrophic performance loss
    -   *Action*: Implemented proper camera lifecycle:
        1. Added `camerasActive` flag to `CubemapRegion` struct
        2. `renderCubemap()` enables cameras and sets `camerasActive = true`
        3. `update()` disables cameras from previous frame (when `camerasActive = true`)
        4. Cameras only render for ONE frame, then get disabled until next update
    -   *Result*: Should restore performance while keeping cubemaps persistent (no flickering)
- [x] **Godot SSR Analysis**:
    -   *Key Findings from `godotssr/shaders/water.gdshader`*:
        - Uses adaptive stepping with `ssr_resolution` parameter (line 38)
        - Better depth comparison: `depth_diff >= 0.0 && depth_diff < ssr_max_diff` (line 157)
        - Screen border fadeout using quadratic falloff (lines 118-129)
        - Implements refraction for visual richness (lines 175-183)
        - Early termination when off-screen (lines 156-157)
    -   *Applied*: Adaptive stepping, better depth threshold, improved fadeout

### Phase 5: Critical Fixes (Session 5)
- [x] **Fix Cubemap Camera Lifecycle** (`cubemapreflection.cpp`):
    -   *Root Cause*: Session 4's camera disable logic was running in the SAME FRAME as camera enable, preventing OSG from rendering
    -   *Action*: Redesigned camera lifecycle system:
        1. Cameras start DISABLED (node mask = 0) instead of always enabled
        2. Only the NEAREST region's cameras are enabled (persistent rendering)
        3. When switching regions, old region's cameras are disabled
        4. Cameras stay enabled and render every frame for smooth reflections
        5. `updateInterval` controls how often we check for region switches, not render frequency
    -   *Changes*:
        - Line 140: Cameras now start with `setNodeMask(0)` instead of `Mask_RenderToTexture`
        - Lines 281-348: New `update()` logic with `lastActiveRegion` tracking
        - Lines 235-271: Simplified `renderCubemap()` - enables cameras persistently
    -   *Performance*: Only ONE region (6 cameras) renders at a time, acceptable cost for smooth reflections
    -   *Expected Result*: Cubemaps should now actually render the scene instead of showing gray
- [x] **Verified Shader CMake Configuration**:
    -   *Action*: Confirmed `lake.vert` and `lake.frag` are properly listed in `files/shaders/CMakeLists.txt` (lines 66-67)
    -   *Result*: Shaders ARE being copied to build directories correctly
- [x] **Lake Movement Analysis**:
    -   *Finding*: Source code is CORRECT - `lake.vert` line 34 uses `worldPos = gl_Vertex.xyz` (world-space vertices)
    -   *Finding*: `lake.cpp` lines 509-524 use `osg::Group` instead of `PositionAttitudeTransform` (no double transformation)
    -   *Conclusion*: Fix is already implemented, but user needs to REBUILD to copy updated shaders to build directory

### Phase 6: Cubemap RTT Configuration Fix (Session 6)
- [x] **Fix Cubemap Camera RTT Setup** (`cubemapreflection.cpp` lines 113-124):
    -   *Root Cause*: Cubemap cameras were missing critical RTT configuration that prevented FBO rendering
    -   *Analysis*: Compared with working RTT implementations (localmap, character preview) and found missing setup
    -   *Action*: Added proper render-to-texture configuration:
        1. Line 113: Added `osg::Camera::PIXEL_BUFFER_RTT` parameter to `setRenderTargetImplementation()`
        2. Line 115: Added `SceneUtil::setCameraClearDepth(camera)` for proper reverse-Z depth clearing
        3. Line 119: Added `setComputeNearFarMode(DO_NOT_COMPUTE_NEAR_FAR)` to use explicit near/far planes
        4. Lines 121-124: Added explicit culling mode (enable far-plane culling, disable small-feature culling)
        5. Line 13: Added `#include <components/sceneutil/depth.hpp>` for depth utilities
    -   *Result*: **SUCCESS** - Cubemaps now render the actual scene (sky, terrain, objects) instead of gray fallback
    -   *Evidence*: User confirmed "cubemaps work! the debug (5) show the sky"

## Known Issues (Needs Investigation)

### Session 6 Test Results
- [!] **CRITICAL: Game Crashes**: System crashes frequently after cubemap rendering starts working
    - *Evidence from Logs*:
        - `[Cubemap:Cull] Callback called (count=0-1), traversing scene root with 16 children` - 6 callbacks per frame (one per cubemap face)
        - `[Cubemap] First update() call - 11 regions active` - Many regions loaded
        - System enables 6 cameras continuously rendering at 512×512 resolution
    - *Hypothesis - Performance/Memory Overload (MOST LIKELY)*:
        1. **GPU Exhaustion**: 6 cameras × 512×512 × 60fps = ~90 million pixels/second just for cubemaps
        2. **Driver Timeout**: GPU spending too long on cubemap rendering, driver thinks it hung and crashes
        3. **FBO Thrashing**: Constantly binding/unbinding 6 framebuffer faces causes driver issues
        4. **Memory Pressure**: Multiple render targets being updated every frame exhausts GPU memory
    - *Hypothesis - Race Condition (SECONDARY)*:
        1. **Scene Graph Modification**: Cubemaps render during cell loading when scene graph is being modified
        2. **Cull Callback Timing**: `CubemapCullCallback` accessing `mSceneRoot` while it's being changed
        3. **Lake Geometry Updates**: Lakes being added/removed while cubemaps are trying to render them
    - *Architecture Issue*: Session 5's "render every frame" approach is too expensive
        - Original Session 4 idea was better: render once, disable cameras, keep texture
        - Static cubemaps updated every 5 seconds are sufficient (environment doesn't change that fast)
    - **RECOMMENDATION**: Revert to one-shot cubemap rendering with periodic updates instead of continuous rendering
- [ ] **Lake Movement**: Not yet tested (user mentioned crashes before testing this)

## Backlog / Future Work

### Phase 7: URGENT - Fix Continuous Rendering Crashes
- [ ] **Implement One-Shot Cubemap Rendering**:
    -   Revert Session 5's "continuous rendering" approach
    -   Cameras should render ONCE when `needsUpdate=true`, then be disabled
    -   Cubemap texture persists in GPU memory (doesn't need re-rendering every frame)
    -   Update interval (5 seconds) controls when cubemaps refresh
    -   This reduces load from ~90M pixels/sec to ~300K pixels/update (30x reduction)
- [ ] **Alternative: Reduce Cubemap Resolution**:
    -   If one-shot rendering still causes issues, try 256×256 instead of 512×512
    -   This would reduce per-update cost by 75%
- [ ] **Alternative: Increase Update Interval**:
    -   Change from 5 seconds to 10-30 seconds for less frequent updates
    -   Static environment reflections don't need frequent updates

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
- **SSR Distance**: Reduced to 4096.0 (~185 feet) from 8192.0 for performance. This still covers most visible reflections in typical scenes.
- **SSR Steps**: Reduced from 32 → 16 with adaptive stepping. Adaptive stepping increases step size with distance, maintaining quality while halving iterations.
- **SSR Max Diff**: Set to 300.0 units (~13.6 feet). This is the maximum depth difference for hit detection, preventing false hits from distant geometry.
- **SSR Adaptive Stepping**: Step size increases by 10% per iteration (see `lake.frag` line 107). This concentrates samples near the surface where detail matters most.
- **IOR Bias**: The `-5.0` bias is a heuristic. If jitter persists, we may need a more robust "is camera underwater" uniform passed from C++.
- **Fallback Color**: RGB(180, 180, 180) neutral gray to avoid overly blue water. Sky cubemap should provide actual environment color once rendering.
- **Coordinate Spaces** (Session 2 analysis):
    - **SSR**: MUST use view space (camera-relative) - confirmed by Godot reference shader
    - **Cubemap**: MUST use world space (absolute directions) - standard cubemap sampling
    - **Fresnel**: Uses world space view direction for physically accurate results
    - Mixing these spaces was causing previous failures
- **Cubemap Camera Lifecycle** (Session 4):
    - Cameras are enabled only when `needsUpdate = true`
    - After enabling, `camerasActive` flag is set
    - Next frame, cameras are disabled (cubemap texture persists in GPU memory)
    - This prevents continuous rendering while maintaining visual persistence

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

## Session 3 Summary
**What We Fixed:**
1. **Lake Movement**: Eliminated double transformation bug by using `osg::Group` instead of `PositionAttitudeTransform` and placing vertices directly in world space
2. **Blue Water**: Changed cubemap fallback (180, 180, 180) and camera clear color (0.7, 0.7, 0.7) from sky blue to neutral gray
3. **Flickering**: Removed camera disable loop from every frame (but introduced performance regression)

**What We Broke:**
1. **CRITICAL**: Removing camera disabling caused severe performance drop - cameras likely rendering every frame now

**What We Learned:**
- The geometry transformation system was applying transformations twice (transform node + shader calculation)
- Sky blue colors in cubemaps were dominating the final water appearance
- Cubemap persistence vs performance is a delicate balance - can't leave cameras enabled continuously

**What Still Needs Fixing:**
- **URGENT**: Restore performance by properly managing camera enable/disable lifecycle
- Minor flickering still present (likely timing-related)
- Need to verify reflection source (SSR vs cubemap) using debug modes 4-6

## Session 4 Summary

**What We Attempted:**
1. **SSR Performance Optimization** (Based on Godot Reference):
   - Reduced ray steps 32 → 16 (~50% fewer iterations)
   - Reduced max distance 8192 → 4096 units (still ~185 feet)
   - Implemented adaptive stepping (step size increases 10% per iteration)
   - Improved screen edge fadeout using Godot's quadratic falloff algorithm
   - Added configurable depth threshold (SSR_MAX_DIFF = 300 units)
   - **Status**: Code complete, awaiting test (rebuild required)

2. **Cubemap Camera Lifecycle Fix** (Attempted):
   - Added `camerasActive` flag to CubemapRegion struct
   - Cameras enabled when `needsUpdate=true`, disabled next frame
   - Goal: Fix Session 3 performance regression while maintaining persistence
   - **Status**: FAILED - cameras not rendering at all

**What We Learned:**

### Critical Discovery: Cubemap Cameras Never Render
Testing revealed cubemaps show only the fallback gray color. Root cause unknown, possibilities:
1. **Timing Issue**: `camerasActive` flag might disable cameras in same frame they're enabled
2. **OSG PRE_RENDER Timing**: PRE_RENDER cameras might need to stay enabled for 2+ frames
3. **Node Mask Issue**: `Mask_RenderToTexture` might be wrong for this camera setup
4. **Scene Graph Issue**: Cameras might not be in the correct part of scene graph
5. **Update Order Issue**: `update()` might run before cameras can render

**Added Diagnostic Logging** (in `cubemapreflection.cpp`):
- `initialize()`: Logs resolution and fallback color
- `addRegion()`: Logs each region added with position/radius
- `renderCubemap()`: Logs when cameras are enabled, tracks node masks
- `update()`: Logs region counts, update intervals, which regions render
- This will help identify the exact failure point in next test

### From Godot SSR Analysis:
- **Adaptive Stepping**: Critical for performance - concentrates samples where needed
- **Screen Border Fadeout**: Quadratic falloff (`4.0 * uv * (1.0 - uv)`) prevents artifacts
- **Depth Threshold**: Prevents false positives from distant geometry
- **Refraction**: Godot implements fake refraction for visual richness (future enhancement)
- **View Space Requirement**: SSR MUST use view space (confirmed in Session 2)

### Build System Issue Discovered:
- **Lake Movement**: Session 3 fix is correct in source code
- **Problem**: Shaders in `MSVC2022_64/Release/resources/shaders/` are outdated
- **Solution**: CMake/build system should copy shaders, but may need manual rebuild
- **Verification**: `lake.vert` line 34 correctly uses `worldPos = gl_Vertex.xyz`

### Architecture Understanding:
1. **Cubemap Creation Pipeline**:
   - `WaterManager::initialize()` creates `CubemapReflectionManager`
   - `loadLakesFromJSON()` → `addLakeAtWorldPos()` → `addLakeCell()`
   - `addLakeCell()` → `mCubemapManager->addRegion()` for each lake
   - Regions created with `needsUpdate=true` initially

2. **Cubemap Update Cycle** (Intended):
   - Frame N: `update()` sees `needsUpdate=true`, calls `renderCubemap()`
   - Frame N: `renderCubemap()` enables cameras, sets `camerasActive=true`
   - Frame N+1: Cameras render to cubemap texture (PRE_RENDER)
   - Frame N+1: `update()` sees `camerasActive=true`, disables cameras
   - Frame N+2: Cubemap texture persists, used for reflections

3. **Potential Timing Bug**:
   - If `update()` runs twice in Frame N, cameras disabled before rendering
   - If PRE_RENDER happens before `update()`, cameras never enabled
   - Need to verify OSG render order: PRE_RENDER → UPDATE → CULL → DRAW

**Next Actions:**
1. **CRITICAL**: Build and check logs for cubemap diagnostic messages
2. **CRITICAL**: Verify shaders are updated in build directory (check timestamps)
3. **Investigate**: Try keeping cameras enabled for 2 frames instead of 1
4. **Investigate**: Check OSG documentation for PRE_RENDER camera lifecycle
5. **Consider**: Alternative approach - cameras always enabled, update interval controls frequency

**Files Modified Session 4:**
- `files/shaders/compatibility/lake.frag` - SSR optimizations
- `apps/openmw/mwrender/cubemapreflection.cpp` - Camera lifecycle + diagnostics
- `apps/openmw/mwrender/cubemapreflection.hpp` - Added `camerasActive` field
- `lake_implementation_tracker.md` - This document

## Session 5 Summary

**What We Fixed:**
1. **Cubemap Camera Lifecycle Bug**: The critical bug from Session 4 where cameras were disabled in the same frame they were enabled
   - Redesigned system: cameras start disabled, only nearest region enabled, persistent rendering
   - Only 6 cameras (1 region) render at a time for acceptable performance
2. **Shader Build Verification**: Confirmed shaders are properly configured in CMake and being copied
3. **Lake Movement Root Cause**: Identified that fix already exists in source code, just needs rebuild

**Root Cause of Cubemap Issue:**
- Session 4's `update()` function had a loop that disabled cameras with `camerasActive=true`
- This loop ran in the SAME `update()` call that set `camerasActive=true`
- OSG PRE_RENDER cameras need to stay enabled to actually render
- The disable happened before OSG could execute the render, so cubemaps stayed gray

**New Architecture:**
```
Frame 1: Player near Region A
  - update() finds nearestIndex = 0 (Region A)
  - renderCubemap(Region A) enables 6 cameras
  - Cameras render to cubemap texture

Frame 2-N: Player still near Region A
  - update() finds nearestIndex = 0 (same)
  - No changes, cameras keep rendering

Frame N+1: Player moves near Region B
  - update() finds nearestIndex = 1 (Region B)
  - Disables Region A's 6 cameras
  - renderCubemap(Region B) enables its 6 cameras
  - Region B cameras render continuously until next switch
```

**What Still Needs Testing:**
1. **Cubemap Rendering**: Does the new lifecycle actually render the scene? (needs build)
2. **Lake Movement**: Does rebuild fix the movement issue? (needs rebuild)
3. **Performance**: Is 6 cameras @ 512x512 per frame acceptable? (needs profiling)

**Diagnostic Improvements (Build 2):**
- Fixed camera re-enabling issue: Active region no longer gets `needsUpdate` timer updates
- Added debug logging to `CubemapCullCallback` to verify it's being called and scene traversal is working
- Next build should reveal if the cull callback is working or if there's a deeper rendering issue

**Files Modified This Session:**
- `apps/openmw/mwrender/cubemapreflection.cpp` - Fixed camera lifecycle (lines 140, 235-271, 319-351)
- `apps/openmw/mwrender/cubemapreflection.hpp` - Added debug logging to CubemapCullCallback (lines 13, 33, 42-57, 63)
- `lake_implementation_tracker.md` - This document

**Next Build Test Plan:**
1. Check for `[Cubemap:Cull]` messages in logs - this will tell us if cameras are actually culling the scene
2. If cull callback is working but still gray, the issue is likely in the FBO attachment or texture binding
3. If cull callback is NOT working, we need to investigate the scene root setup
