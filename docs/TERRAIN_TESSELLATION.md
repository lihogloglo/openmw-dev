# GPU Tessellation for OpenMW Terrain

## Overview

This document describes the GPU tessellation system implemented for OpenMW terrain as an alternative to CPU-based subdivision for snow deformation effects. The tessellation system uses OpenGL 4.0+ hardware tessellation to dynamically subdivide terrain geometry based on camera distance.

**Tessellation Type: Quad-based (Quadgrid)**

The system uses quad tessellation rather than triangle tessellation, which is more natural for heightmap-based terrain since terrain grids are organized as quads.

## Background

### Problem Statement

The original snow deformation system required high vertex density to render smooth footprints and deformation trails. The initial solution used CPU-based subdivision (`TerrainSubdivider`) which:

- Subdivides each terrain chunk on the CPU
- Creates multiple subdivision levels based on distance from the player
- Caches subdivided geometry
- Consumes significant memory for stored geometry
- Has fixed subdivision levels (not smooth transitions)

### Solution: Hardware Tessellation

GPU tessellation moves the subdivision work to the graphics hardware:

- Base mesh geometry remains low-poly
- Tessellation Control Shader determines subdivision level per-triangle
- Tessellation Evaluation Shader generates new vertices and applies displacement
- Dynamic LOD based on camera distance (smooth transitions)
- Lower memory footprint (no cached subdivided geometry)
- Requires OpenGL 4.0+ (optional feature, falls back gracefully)

## Architecture

### Shader Pipeline

```
┌─────────────────┐
│  Vertex Shader  │  terrain.vert - Passes vertex data through
└────────┬────────┘
         ▼
┌─────────────────┐
│  Tessellation   │  terrain.tesc - Calculates tessellation levels
│ Control Shader  │  based on edge distance to camera
└────────┬────────┘
         ▼
┌─────────────────┐
│  Tessellation   │  terrain.tese - Interpolates new vertices,
│Evaluation Shader│  samples displacement map, applies deformation
└────────┬────────┘
         ▼
┌─────────────────┐
│ Fragment Shader │  terrain.frag - Lighting, POM, normal perturbation
└─────────────────┘
```

### File Structure

```
files/shaders/core/
├── terrain.vert   # Vertex shader (GLSL 4.0 compatibility)
├── terrain.tesc   # Tessellation control shader
├── terrain.tese   # Tessellation evaluation shader
└── terrain.frag   # Fragment shader (GLSL 4.0 compatibility)

components/shader/
├── shadermanager.hpp  # Added getTessellationProgram()
└── shadermanager.cpp  # Implementation

components/terrain/
├── buffercache.hpp    # Added getPatchIndexBuffer()
├── buffercache.cpp    # GL_PATCHES primitive support
├── material.hpp       # Added createTessellationPasses()
├── material.cpp       # Tessellation pass creation
├── chunkmanager.hpp   # Added tessellation enable flag
└── chunkmanager.cpp   # Integration with chunk creation
```

---

## CRITICAL: OpenSceneGraph Tessellation Requirements

### GLSL Profile: Use Compatibility, NOT Core

**OpenMW uses GLSL compatibility profile throughout the codebase.** All shaders use built-in variables like `gl_Vertex`, `gl_Normal`, `gl_ModelViewMatrix`, etc.

**WRONG - Core Profile (will NOT work in OpenMW):**
```glsl
#version 400 core
layout(location = 0) in vec3 osg_Vertex;
layout(location = 2) in vec3 osg_Normal;
uniform mat4 osg_ModelViewMatrix;  // NOT provided by OSG without aliasing!
```

**CORRECT - Compatibility Profile:**
```glsl
#version 400 compatibility
// Use built-in vertex attributes
void main() {
    vs_out.position = gl_Vertex.xyz;
    vs_out.normal = gl_Normal;
    vs_out.color = gl_Color;
    vs_out.texCoord = gl_MultiTexCoord0.xy;
}
```

### Why Compatibility Profile is Required

1. **OSG Vertex Attribute Aliasing**: OSG can alias `gl_Vertex` → `osg_Vertex`, but this requires calling `setUseVertexAttributeAliasing(true)` on the State. OpenMW does NOT enable this.

2. **Built-in Matrix Uniforms**: In compatibility profile, `gl_ModelViewMatrix`, `gl_NormalMatrix` are available. However, **do NOT use `gl_ProjectionMatrix`** - see below.

3. **Existing Codebase**: All OpenMW shaders (in `files/shaders/compatibility/`) use GLSL 1.20 with `gl_*` built-ins. The tessellation shaders must be consistent.

### CRITICAL: Reverse-Z Depth Buffer - Use projectionMatrix Uniform

**OpenMW uses a reverse-Z depth buffer** for improved depth precision. This requires a custom projection matrix that is NOT stored in `gl_ProjectionMatrix`.

**WRONG - Using gl_ProjectionMatrix (terrain renders over everything):**
```glsl
gl_Position = gl_ProjectionMatrix * viewPos;  // BROKEN - wrong depth values!
```

**CORRECT - Using custom projectionMatrix uniform:**
```glsl
uniform mat4 projectionMatrix;  // Custom uniform set by OpenMW
// ...
gl_Position = projectionMatrix * viewPos;  // Correct reverse-Z depth
```

All OpenMW shaders include `lib/core/vertex.glsl` which declares this uniform and provides `modelToClip()` / `viewToClip()` helpers. For tessellation shaders where you can't use `#include`, declare the uniform directly.

### OSG Tessellation Setup (from official examples)

Based on [osgtessellationshaders.cpp](https://github.com/openscenegraph/OpenSceneGraph/blob/master/examples/osgtessellationshaders/osgtessellationshaders.cpp):

```cpp
// 1. Create geometry with GL_PATCHES primitive
geometry->addPrimitiveSet(new osg::DrawElementsUInt(
    osg::PrimitiveSet::PATCHES, indexCount, indices));

// 2. Set patch parameter (vertices per patch)
state->setAttribute(new osg::PatchParameter(4));  // Quad patches

// 3. Add shader program with all 4 stages
program->addShader(vertexShader);
program->addShader(tessControlShader);
program->addShader(tessEvalShader);
program->addShader(fragmentShader);

// 4. Bind custom attributes
program->addBindAttribLocation("terrainWeights", 6);
```

### OSG Default Vertex Attribute Locations

When using `setUseVertexAttributeAliasing(true)` (NOT used in OpenMW), OSG maps:

| Slot | Alias Name | Legacy Built-in |
|------|------------|-----------------|
| 0 | osg_Vertex | gl_Vertex |
| 2 | osg_Normal | gl_Normal |
| 3 | osg_Color | gl_Color |
| 8+ | osg_MultiTexCoord0+ | gl_MultiTexCoord0+ |

**Since OpenMW doesn't use aliasing, we use the built-ins directly.**

---

## Implementation Details

### 1. Shader Manager Extensions

**New Method: `getTessellationProgram()`**

```cpp
osg::ref_ptr<osg::Program> ShaderManager::getTessellationProgram(
    const std::string& templateName,
    const DefineMap& defines,
    const osg::Program* programTemplate)
```

- Loads all four shader stages from `core/` directory
- Files: `{templateName}.vert`, `{templateName}.tesc`, `{templateName}.tese`, `{templateName}.frag`
- Returns nullptr if any shader fails to load
- **Must call `addLinkedShaders()` for ALL four stages** (not just vert/frag)

### 2. Buffer Cache Extensions

**New Method: `getPatchIndexBuffer()`**

```cpp
osg::ref_ptr<osg::DrawElements> BufferCache::getPatchIndexBuffer(
    unsigned int numVerts,
    unsigned int flags)
```

- Creates index buffers with `GL_PATCHES` primitive mode
- Triangle patches (3 vertices per patch)
- Same index data as regular buffers, different primitive type
- Cached separately from triangle index buffers

### 3. Material Extensions

**New Function: `createTessellationPasses()`**

```cpp
std::vector<osg::ref_ptr<osg::StateSet>> createTessellationPasses(
    Resource::SceneManager* sceneManager,
    const std::vector<TextureLayer>& layers,
    const std::vector<osg::ref_ptr<osg::Texture2D>>& blendmaps,
    int blendmapScale,
    float layerTileSize,
    bool esm4terrain)
```

- Creates state sets with tessellation shaders
- Sets `osg::PatchParameter(3)` for triangle patches
- Binds custom vertex attribute (`terrainWeights` at location 6)
- Sets tessellation-specific uniforms
- Returns empty vector on failure (triggers fallback)

### 4. Chunk Manager Integration

**New Flag: `mTessellationEnabled`**

```cpp
void setTessellationEnabled(bool enabled);
bool getTessellationEnabled() const;
```

**Modified Behavior:**

- `createPasses()`: Uses `createTessellationPasses()` when enabled
- `createChunk()`: Uses `getPatchIndexBuffer()` when enabled
- Adds `cameraPos` uniform for tessellation LOD calculation
- Automatic fallback to regular shaders on failure

---

## Shader Details

### Vertex Shader (terrain.vert)

**Uses compatibility profile with built-in vertex attributes:**

```glsl
#version 400 compatibility

out VS_OUT {
    vec3 position;       // Local chunk position
    vec3 worldPosition;  // World position (for LOD + deformation)
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} vs_out;

attribute vec4 terrainWeights;  // Custom attribute at location 6
uniform vec3 chunkWorldOffset;

void main() {
    vs_out.position = gl_Vertex.xyz;  // LOCAL position
    vs_out.worldPosition = gl_Vertex.xyz + chunkWorldOffset;  // WORLD position
    vs_out.normal = gl_Normal;
    vs_out.color = gl_Color;
    vs_out.texCoord = gl_MultiTexCoord0.xy;
    vs_out.terrainWeights = terrainWeights;
}
```

**Key Design Decision**: Pass BOTH local and world positions:
- `position`: Used for transformation via `gl_ModelViewMatrix` in TES AND for LOD calculation in TCS
- `worldPosition`: Used for deformation UV lookup only (NOT for LOD calculation!)

**CRITICAL**: `cameraPos` is set from `CullVisitor::getEyePoint()` which returns the eye position in **local/model space**, not world space. Therefore, LOD calculation must use `position` (local), not `worldPosition` (world). Using world positions causes a coordinate space mismatch and results in incorrect tessellation levels.

### Tessellation Control Shader (terrain.tesc)

Calculates tessellation level based on camera distance:

```glsl
#version 400 compatibility

layout(vertices = 4) out;  // Quad patches

uniform vec3 cameraPos;           // Camera position in LOCAL/MODEL space (from CullVisitor::getEyePoint())
uniform float tessMinDistance;    // 500.0 (default, ~23 feet / ~7 meters)
uniform float tessMaxDistance;    // 5000.0 (default, ~230 feet / ~70 meters)
uniform float tessMinLevel;       // 1.0
uniform float tessMaxLevel;       // 16.0

// Use LOCAL positions - cameraPos is in local/model space!
float calcTessLevel(vec3 localPos0, vec3 localPos1) {
    vec3 edgeMidpoint = (localPos0 + localPos1) * 0.5;
    float dist = length(edgeMidpoint - cameraPos);
    float t = clamp((dist - tessMinDistance) /
                    (tessMaxDistance - tessMinDistance), 0.0, 1.0);
    return mix(tessMaxLevel, tessMinLevel, t);
}

void main() {
    // Pass through control point data
    tcs_out[gl_InvocationID] = tcs_in[gl_InvocationID];

    if (gl_InvocationID == 0) {
        // Quad vertices: 0=bottom-left, 1=bottom-right, 2=top-right, 3=top-left
        vec3 p0 = tcs_in[0].position;
        vec3 p1 = tcs_in[1].position;
        vec3 p2 = tcs_in[2].position;
        vec3 p3 = tcs_in[3].position;

        // For quads: outer[0]=left, outer[1]=bottom, outer[2]=right, outer[3]=top
        gl_TessLevelOuter[0] = calcTessLevel(p0, p3);  // left edge
        gl_TessLevelOuter[1] = calcTessLevel(p0, p1);  // bottom edge
        gl_TessLevelOuter[2] = calcTessLevel(p1, p2);  // right edge
        gl_TessLevelOuter[3] = calcTessLevel(p3, p2);  // top edge

        // Inner levels for horizontal and vertical subdivision
        gl_TessLevelInner[0] = (gl_TessLevelOuter[1] + gl_TessLevelOuter[3]) * 0.5;
        gl_TessLevelInner[1] = (gl_TessLevelOuter[0] + gl_TessLevelOuter[2]) * 0.5;
    }
}
```

### Tessellation Evaluation Shader (terrain.tese)

Generates new vertices and applies displacement using bilinear interpolation for quads:

```glsl
#version 400 compatibility

layout(quads, equal_spacing, ccw) in;

uniform sampler2D snowDeformationMap;
uniform vec3 snowRTTWorldOrigin;
uniform float snowRTTScale;
uniform bool snowDeformationEnabled;

// Bilinear interpolation for quads
// Quad vertices: 0=bottom-left, 1=bottom-right, 2=top-right, 3=top-left
vec3 interpolate3(vec3 v0, vec3 v1, vec3 v2, vec3 v3) {
    vec3 bottom = mix(v0, v1, gl_TessCoord.x);  // bottom edge
    vec3 top = mix(v3, v2, gl_TessCoord.x);     // top edge
    return mix(bottom, top, gl_TessCoord.y);    // interpolate vertically
}

void main() {
    // Interpolate BOTH local and world positions using bilinear interpolation
    vec3 localPosition = interpolate3(tes_in[0].position, tes_in[1].position,
                                       tes_in[2].position, tes_in[3].position);
    vec3 worldPosition = interpolate3(tes_in[0].worldPosition, tes_in[1].worldPosition,
                                       tes_in[2].worldPosition, tes_in[3].worldPosition);

    // Apply displacement using WORLD position for UV calculation
    if (snowDeformationEnabled && baseLift > 0.01) {
        vec2 deformUV = (worldPosition.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
        float deformationFactor = texture(snowDeformationMap, deformUV).r;
        float zOffset = baseLift * (1.0 - deformationFactor);

        localPosition.z += zOffset;
        worldPosition.z += zOffset;
    }

    // Transform LOCAL position using compatibility profile matrices
    vec4 viewPos = gl_ModelViewMatrix * vec4(localPosition, 1.0);
    gl_Position = projectionMatrix * viewPos;  // Use custom projection for reverse-Z
}
```

### Fragment Shader (terrain.frag)

Handles lighting and visual effects using compatibility profile:

```glsl
#version 400 compatibility

// Use gl_NormalMatrix, gl_ModelViewMatrixInverse (built-in)
viewNormal = normalize(gl_NormalMatrix * fs_in.normal);
mat4 viewMatrixInverse = gl_ModelViewMatrixInverse;
```

---

## Known Issues and Solutions

### Issue: Conflict with CPU Subdivision System

**Symptom**: Terrain disappears in areas where `TerrainSubdivider` is active (chunks near the player).

**Root Cause**: When tessellation is enabled, the chunkmanager uses `getPatchIndexBuffer()` which creates `GL_PATCHES` primitives. However, the CPU subdivision system (`TerrainSubdivider`) creates new geometry with `GL_TRIANGLES` primitives that doesn't go through the tessellation shader path.

**Solutions**:

1. **Disable CPU subdivision when tessellation is enabled**: The two systems should be mutually exclusive.

2. **Modify TerrainSubdivider to use GL_PATCHES**: Update the subdivision output to also use patch primitives and apply the tessellation shader.

3. **Use tessellation only for distant chunks**: Keep CPU subdivision for near chunks where you need maximum control.

### Issue: Terrain Disappears Completely

**Root Cause (SOLVED)**: Shaders were using `#version 400 core` with `osg_Vertex`, `osg_ModelViewMatrix`, etc. OpenMW doesn't enable vertex attribute aliasing, so these uniforms/attributes weren't available.

**Solution**: Changed all tessellation shaders to `#version 400 compatibility` and use built-in `gl_Vertex`, `gl_Normal`, `gl_ModelViewMatrix`, etc.

### Issue: Terrain Renders Over Everything (SOLVED)

**Symptom**: Terrain appears to render on top of all other objects, or distant terrain looks corrupted.

**Root Cause**: Composite map chunks (large/distant terrain) were using `GL_PATCHES` primitive mode but non-tessellation shaders. Rendering `GL_PATCHES` without tessellation shaders causes undefined behavior - some drivers draw nothing, others draw garbage.

**Solution**: Only use `GL_PATCHES` for non-composite-map chunks that actually use tessellation shaders. Composite map chunks use regular `GL_TRIANGLES` with standard shaders:

```cpp
// In ChunkManager::createChunk()
bool useCompositeMap = chunkSize >= mCompositeMapLevel;

if (mTessellationEnabled && !useCompositeMap)
    geometry->addPrimitiveSet(mBufferCache.getPatchIndexBuffer(numVerts, lodFlags));
else
    geometry->addPrimitiveSet(mBufferCache.getIndexBuffer(numVerts, lodFlags));
```

### Issue: OpenGL "invalid operation" Errors

**Possible Causes**:

1. **Shader compilation failure**: Check log for shader errors
2. **Missing uniforms**: Ensure all uniforms declared in shaders are set
3. **Primitive type mismatch**: Ensure geometry uses `GL_PATCHES`, not `GL_TRIANGLES`
4. **PatchParameter not set**: Must call `stateset->setAttribute(new osg::PatchParameter(3))`

---

## Usage

### Enabling Tessellation

```cpp
// In terrain initialization
chunkManager->setTessellationEnabled(true);
```

### Configuration Settings

```ini
[Terrain]
; Enable GPU tessellation (requires OpenGL 4.0+)
tessellation = true

; Tessellation LOD parameters
tessellation min distance = 50.0
tessellation max distance = 500.0
tessellation min level = 1.0
tessellation max level = 16.0
```

### Runtime Requirements

- OpenGL 4.0+ capable GPU
- Driver support for tessellation shaders
- Graceful fallback to CPU subdivision if unavailable

---

## Tessellation Parameters

| Uniform | Type | Default | Description |
|---------|------|---------|-------------|
| `cameraPos` | vec3 | viewPoint | Camera position for LOD calculation (in local/model space) |
| `tessMinDistance` | float | 500.0 | Distance for maximum tessellation (~23 feet / ~7m) |
| `tessMaxDistance` | float | 5000.0 | Distance for minimum tessellation (~230 feet / ~70m) |
| `tessMinLevel` | float | 1.0 | Minimum subdivision level |
| `tessMaxLevel` | float | 16.0 | Maximum subdivision level |

Note: Morrowind uses ~22.1 units per foot. The default distances are chosen to provide smooth deformation within typical player interaction range.

---

## Comparison: CPU Subdivision vs GPU Tessellation

| Aspect | CPU Subdivision | GPU Tessellation |
|--------|-----------------|------------------|
| **Processing** | CPU-bound | GPU-bound |
| **Memory** | High (cached geometry) | Low (base mesh only) |
| **LOD Transitions** | Discrete levels | Smooth, continuous |
| **Chunk Boundaries** | Potential seams | Seamless (per-edge LOD) |
| **Compatibility** | All GPUs | OpenGL 4.0+ only |
| **Implementation** | TerrainSubdivider class | Tessellation shaders |
| **Patch Type** | N/A (triangles) | Quads (natural for heightmaps) |
| **Interpolation** | Barycentric | Bilinear (gl_TessCoord.xy) |

---

## Implementation Status

### ✓ Dynamic Camera Updates (IMPLEMENTED)

The `cameraPos` uniform is now dynamically updated each frame in `TerrainDrawable::cull()`:

```cpp
// In terraindrawable.cpp
void TerrainDrawable::cull(osgUtil::CullVisitor* cv) {
    if (stateset) {
        osg::Uniform* cameraPosUniform = stateset->getUniform("cameraPos");
        if (cameraPosUniform) {
            osg::Vec3f eyePoint = cv->getEyePoint();
            cameraPosUniform->set(eyePoint);
        }
    }
}
```

### ✓ Settings Integration (IMPLEMENTED)

Tessellation can be toggled and configured via settings.

### ✓ Compatibility Profile Shaders (IMPLEMENTED)

All tessellation shaders now use `#version 400 compatibility` with built-in vertex attributes and matrices.

### ✓ CPU Subdivision Conflict (FIXED)

CPU subdivision (`TerrainSubdivider`) is now automatically disabled when tessellation is enabled. This is done in `ChunkManager::getChunk()`:

```cpp
// Skip CPU subdivision when GPU tessellation is enabled
if (!mTessellationEnabled)
{
    // ... CPU subdivision logic ...
}
```

The two systems are mutually exclusive because:
- CPU subdivision creates geometry with `GL_TRIANGLES` primitive sets
- GPU tessellation requires `GL_PATCHES` primitive sets
- GPU tessellation achieves the same goal (more vertices for smooth deformation) but more efficiently

---

## Future Improvements

### 1. Adaptive Tessellation

Consider screen-space error metrics instead of pure distance:

```glsl
float calcTessLevel(vec3 pos0, vec3 pos1) {
    vec4 clip0 = mvp * vec4(pos0, 1.0);
    vec4 clip1 = mvp * vec4(pos1, 1.0);
    vec2 screen0 = clip0.xy / clip0.w * screenSize;
    vec2 screen1 = clip1.xy / clip1.w * screenSize;
    float screenLength = length(screen1 - screen0);
    return clamp(screenLength / targetPixels, minLevel, maxLevel);
}
```

### 2. Displacement Map Improvements

- Use higher resolution deformation maps
- Implement displacement map mipmapping
- Add edge displacement for chunk boundary continuity

### 3. Performance Optimization

- Profile tessellation overhead
- Consider geometry shader for wireframe debugging
- Implement tessellation LOD culling (skip tessellation for distant chunks)

---

## Debugging

### Tessellation Level Visualization

Uncomment in `terrain.frag`:

```glsl
float tessNormalized = clamp((fs_in.tessLevel - 1.0) / 15.0, 0.0, 1.0);
vec3 tessDebugColor = mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), tessNormalized);
fragColor.rgb = mix(fragColor.rgb, tessDebugColor, 0.4);
```

### Common Issues

1. **Black terrain**: Check shader compilation errors in log
2. **No tessellation visible**: Verify GL_PATCHES primitive mode is set
3. **Cracks at boundaries**: Ensure matching tessellation levels at edges
4. **Performance issues**: Lower `tessMaxLevel` or increase `tessMinDistance`
5. **Terrain disappears**: Check GLSL profile (must be compatibility, not core)

---

## References

- [OpenGL Tessellation Wiki](https://www.khronos.org/opengl/wiki/Tessellation)
- [OSG Tessellation Example](https://github.com/openscenegraph/OpenSceneGraph/blob/master/examples/osgtessellationshaders/osgtessellationshaders.cpp)
- [OSG Vertex Attribute Aliasing Discussion](https://osg-users.openscenegraph.narkive.com/8nXnCbaY/using-modern-shaders-with-osg-setting-vertex-attribute-layout)
- [OSG State.cpp - Vertex Attribute Aliases](https://github.com/openscenegraph/OpenSceneGraph/blob/master/src/osg/State.cpp)
- [GPU Gems 2: Terrain Rendering](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
