# SSR & Lake System Tracker

This document tracks the progress, audit findings, and test results for the OpenMW Lake system and SSR implementation.

## Audit Findings

### Dead Code / Unused Variables
- [x] `Lake::mDefaultHeight`: Removed.
- [x] `LakeStateSetUpdater::mLastLogFrame`: Removed.
- [x] `Lake::update`: Verified empty (kept for interface compliance).

### Weird Behavior / Issues
- [x] **Time Update**: Fixed. Now using `cv->getFrameStamp()->getSimulationTime()`.
- [x] **Camera Position**: Fixed. Now using `cv->getEyeLocal()` which is more robust.
- [x] **Altitude Issue**: Fixed.
    - Cause: `invViewMatrix` uniform was likely not updating correctly or had precision/timing issues, causing `worldPos` in the shader to be incorrect (often falling back to View Space). This made the lake texture/ripples move with the camera.
    - Fix: Implemented robust world position calculation in `lake.vert` using a new per-cell `cellPos` uniform. `worldPos = cellPos + gl_Vertex.xyz`. This bypasses the View Matrix entirely for position calculation.

## Implementation Tasks

### 1. Cleanup & Fixes
- [x] Remove dead code (`mLastLogFrame`, `mDefaultHeight`).
- [x] Fix time update to use `osg::FrameStamp`.
- [x] Consolidate camera position calculation.
- [x] Fix Lake Altitude issue (NodeMask + CellPos Uniform).

### 2. SSR Implementation
- [ ] Verify `SSRManager` integration.
- [ ] Verify `lake.frag` SSR sampling.
- [ ] Test SSR reflections.

### 3. Cubemap Fallback
- [ ] Verify `CubemapReflectionManager` integration.
- [ ] Verify `lake.frag` cubemap sampling.
- [ ] Test cubemap fallback when SSR is missing or invalid.

## Test Log

| Date | Test | Result | Notes |
|------|------|--------|-------|
|      |      |        |       |
