# Mesh Blending System Audit

## Executive Summary
The current implementation of mesh blending reuses the existing "Soft Particle" shader logic (`soft.glsl`). While this logic works for semi-transparent particles like smoke or fire, it is ill-suited for solid objects like rocks due to hardcoded opacity limits and view-dependent fading.

## Findings

### 1. Shader Logic (`soft.glsl`) is Tuned for Particles
The `calcSoftParticleFade` function in `files/shaders/lib/particle/soft.glsl` contains magic numbers and logic specifically for volumetric particles:

*   **Hardcoded Opacity Limit (`shift`)**:
    ```glsl
    const float shift = 0.845;
    return shift * pow(clamp(delta/falloff, 0.0, 1.0), contrast) * viewBias;
    ```
    The `shift` constant limits the maximum alpha to `0.845`. This means "solid" rocks will always be 15.5% transparent, making them look ghostly or washed out.

*   **View-Dependent Fading (`viewBias`)**:
    ```glsl
    if (fade)
    {
        float VdotN = dot(viewDir, viewNormal);
        viewBias = abs(VdotN) * quickstep(euclidianDepth / softFalloffDepth) * (1.0 - pow(1.0 - abs(VdotN), 1.3));
    }
    ```
    When `fade` (controlled by `particleFade` uniform) is true, the shader fades the object based on:
    1.  **Distance to Camera**: `quickstep(euclidianDepth / softFalloffDepth)`. Objects close to the camera fade out.
    2.  **Viewing Angle**: `abs(VdotN)`. Surfaces parallel to the view direction fade out.
    
    This behavior is desirable for sprites to avoid clipping the camera, but incorrect for solid meshes blending with terrain.

### 2. Uniform Defaults in `shadervisitor.cpp`
*   **`particleFade`**: Defaults to `1.0` (true). This enables the `viewBias` logic described above, causing rocks to fade out when close to the camera or viewed at glancing angles.
*   **`particleSize`**: Defaults to `100.0`. This controls the blend width (`falloff = size * 0.33`). A value of 100 might be appropriate, but it's the primary control for the blend gradient.
*   **`softFalloffDepth`**: Defaults to `500.0`. This controls the *camera distance* at which the object fades out (via `viewBias`), NOT the intersection blend width.

### 3. Depth Writing
*   The system correctly forces `TRANSPARENT_BIN` and `SceneUtil::AutoDepth` (enabling depth write). This is generally correct for "solid" transparent objects, but combined with the `shift < 1.0` issue, it might lead to visible sorting artifacts if the rock is partially transparent.

## Recommendations

### 1. Create a Dedicated Mesh Blending Shader Function
Instead of reusing `calcSoftParticleFade` exactly as is, we should either:
*   Parameterize `soft.glsl` to accept a `maxOpacity` (shift) and disable `viewBias`.
*   Or create a separate `calcMeshBlendFade` function in a new file or within `soft.glsl`.

### 2. Update `shadervisitor.cpp`
*   **Disable `particleFade`**: For mesh blending nodes (rocks), set `particleFade` uniform to `0.0` to disable the view-dependent fading.
*   **Adjust `particleSize`**: Ensure `particleSize` is set to a value that represents the desired blend width (e.g., 50-100 units).

### 3. Proposed Fix Implementation
Modify `files/shaders/lib/particle/soft.glsl` to allow full opacity and modify `components/shader/shadervisitor.cpp` to configure the uniforms correctly for rocks.

#### `soft.glsl` Change
Add a `solid` parameter or separate function. For minimal disruption, we can just change `shift` to `1.0` if `fade` is false, or pass a new uniform.
However, `shift` is hardcoded inside the function.

**Better Approach**:
Pass `shift` as an argument or assume `shift = 1.0` when `fade` is false?
Actually, `fade` controls `viewBias`. If `fade` is false, `viewBias` is 1.0.
If we also make `shift` 1.0 when `fade` is false, that might solve both problems.

**Proposed Logic Change in `soft.glsl`**:
```glsl
    // ...
    float viewBias = 1.0;
    float maxOpacity = 1.0; // Default to full opacity

    if (fade)
    {
        float VdotN = dot(viewDir, viewNormal);
        viewBias = abs(VdotN) * quickstep(euclidianDepth / softFalloffDepth) * (1.0 - pow(1.0 - abs(VdotN), 1.3));
        maxOpacity = 0.845; // Use legacy shift for soft particles
    }

    const float contrast = 1.30;
    // ...
    return maxOpacity * pow(clamp(delta/falloff, 0.0, 1.0), contrast) * viewBias;
```

#### `shadervisitor.cpp` Change
*   When detecting "rock" or `sXMeshBlend`:
    *   Set `particleFade` to `0.0` (false).
    *   Keep `particleSize` as is (or expose it to artists via user values).
