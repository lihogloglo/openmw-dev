# Lake System Audit

## 1. Architecture Overview

The Lake system is designed to support multi-level water bodies with Screen-Space Reflections (SSR) and Cubemap fallbacks. It is integrated into the existing `WaterManager` and uses a cell-based approach for rendering.

### Key Components

*   **`WaterManager` (`apps/openmw/mwrender/water.cpp`)**:
    *   Orchestrates `Ocean`, `Lake`, and `CubemapReflectionManager`.
    *   Manages `WaterHeightField` for detecting water height at specific positions.
    *   Provides scene buffers (`sceneColor`, `sceneDepth`) to `Lake` for SSR.
    *   Adds/Removes lake cells and corresponding cubemap regions.

*   **`Lake` (`apps/openmw/mwrender/lake.cpp`)**:
    *   Manages a collection of `CellWater` objects (quads).
    *   Handles geometry creation using a robust `cellCenter` + `gl_Vertex` approach to avoid floating-point precision issues.
    *   Uses `LakeStateSetUpdater` to bind uniforms and textures per frame.

*   **`CubemapReflectionManager` (`apps/openmw/mwrender/cubemapreflection.cpp`)**:
    *   Manages a pool of dynamic cubemaps.
    *   Updates cubemaps based on camera position and priority.
    *   Provides a fallback cubemap (currently uninitialized/black).

*   **Shaders**:
    *   `lake.vert`: Handles vertex transformation and world position calculation.
    *   `lake.frag`: Implements SSR raymarching and cubemap mixing.

## 2. Critical Issues & Weak Links

### A. SSR Implementation (Why you can't see SSR)

The primary reason SSR is likely invisible is the **raymarching distance**.

*   **Issue**: In `lake.frag`, `SSR_MAX_DISTANCE` is set to `50.0`.
    *   In Morrowind units, 22.1 units = 1 foot.
    *   **50.0 units is approximately 2.2 feet.**
    *   This means reflections will only show objects within ~2 feet of the reflection point. For a lake, this is practically nothing.
*   **Fix**: Increase `SSR_MAX_DISTANCE` to a reasonable value, e.g., `5000.0` (approx 225 feet) or more, depending on the desired range.
*   **Secondary Issue**: The `traceSSR` function uses `gl_ProjectionMatrix` to project ray positions. Ensure this matrix is correct for the current view (especially with Reverse-Z).

### B. Cubemap Fallback (Why you can't see Cubemaps)

*   **Issue**: The `mFallbackCubemap` in `CubemapReflectionManager` is initialized but **never rendered to**.
    *   It is allocated as `GL_RGB8` but no data is ever uploaded or rendered into it.
    *   Result: It renders as black.
*   **Fix**: The fallback cubemap should be rendered at least once (e.g., capturing the sky) or updated periodically. Alternatively, use a static sky cubemap.
*   **Region Logic**: `WaterManager` adds regions for lakes, but if the camera is outside the radius of any region, it falls back to the (black) fallback cubemap.

### C. Lake Movement / Altitude Jitter

*   **Observation**: "They move up and down depending on the camera angle."
*   **Analysis**:
    *   `lake.vert` uses `worldPos = cellCenter + gl_Vertex.xyz`. This is robust.
    *   However, `gl_Position` relies on `gl_ModelViewProjectionMatrix`.
    *   If the "Height Test" (underwater check) is unstable, it might be switching rendering modes or IOR.
    *   **IOR Logic**: `lake.frag` flips IOR based on `cameraWorldPos.z > worldPos.z`. If the camera is near the water surface, this could flip-flop, causing visual popping.
    *   **Projection Issue**: If the lake geometry is very large (flat), precision issues in the depth buffer (Z-fighting with far plane?) might cause visual vertical jitter.

### D. Color / Altitude Appearance

*   **Observation**: "Lakes appear generally blue (turquoise) until I reach a certain altitude."
*   **Analysis**:
    *   `lake.frag` uses a dark `WATER_COLOR`.
    *   Since SSR (short distance) and Cubemap (black fallback) are likely contributing near-zero light, the water looks dark/flat.
    *   At higher altitudes, the Fresnel term changes (view angle becomes steeper/shallower), potentially mixing in more of the (black) reflection or the water color.
    *   If `fog` is applied, it might tint the water blue/turquoise at distance.

### E. Missing Files

*   **`ssrmanager.cpp`**: The user mentioned this file, but it does not exist. The SSR logic is currently inline in `lake.frag` and `LakeStateSetUpdater`. A dedicated `SSRManager` might be cleaner.

## 3. Recommendations

1.  **Fix SSR Distance**: Change `SSR_MAX_DISTANCE` in `lake.frag` to `5000.0` or higher.
2.  **Fix Fallback Cubemap**: Implement logic to render the sky into `mFallbackCubemap` so it's not black.
3.  **Debug Mode**: Use the existing debug modes in `lake.frag` (e.g., `debugMode = 4` for SSR only) to verify the fix.
4.  **Investigate Altitude Jitter**: Check if the jitter correlates with `isUnderwater` switching.
