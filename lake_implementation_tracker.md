# Lake System Master Tracker

# Goal
Comprehensive overhaul and stabilization of the Multi-Level Lake System. This document tracks all implementation steps, test results, and architectural improvements.

## Status Overview
- **Current Phase**: Verification of Visual Fixes
- **Last Action**: Applied critical fixes for SSR visibility, black water, and surface jitter.
- **Next Step**: User verification in-game.

## Completed Tasks

### Phase 1: Audit & Critical Fixes (Session 1)
- [x] **System Audit**: Analyzed `WaterManager`, `Lake`, `CubemapReflectionManager`, and shaders.
    -   *Findings*: SSR distance was too short (2ft), fallback cubemap was uninitialized (black), `ssrmanager.cpp` missing.
    -   *Reference*: [Lake_System_Audit.md](file:///d:/Gamedev/OpenMW/openmw-dev-waterlevels/Lake_System_Audit.md)
- [x] **Fix SSR Visibility**:
    -   *Action*: Increased `SSR_MAX_DISTANCE` in `lake.frag` from `50.0` to `8192.0`.
    -   *Result*: Reflections should now cover a reasonable distance (1 cell).
- [x] **Fix Black Water Fallback**:
    -   *Action*: Initialized `mFallbackCubemap` in `cubemapreflection.cpp` with a static "Sky Blue" color.
    -   *Result*: Water no longer turns pitch black when outside a reflection region.
- [x] **Fix Surface Jitter**:
    -   *Action*: Added `-5.0` bias to IOR check in `lake.frag`.
    -   *Result*: Prevents IOR flipping/flickering at water surface level.

## Current Tasks (Verification)

### Phase 2: Verification
- [ ] **Verify SSR**: Check if distant objects reflect on the lake surface.
- [ ] **Verify Fallback**: Check if water is blue (not black) in areas without active lake regions.
- [ ] **Verify Stability**: Check for vertical jitter when moving camera near water surface.

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
- **IOR Bias**: The `-5.0` bias is a heuristic. If jitter persists, we may need a more robust "is camera underwater" uniform passed from C++.
- **Fallback Color**: RGB(135, 206, 235) is a placeholder. Ideally, this should sample the current fog/sky color uniform.
