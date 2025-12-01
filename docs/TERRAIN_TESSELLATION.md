# GPU Tessellation for OpenMW Terrain

## Overview

This document describes the GPU tessellation system implemented for OpenMW terrain as an alternative to CPU-based subdivision for snow deformation effects. The tessellation system uses OpenGL 4.0+ hardware tessellation to dynamically subdivide terrain geometry based on camera distance.

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
├── terrain.vert   # Vertex shader (GLSL 4.0)
├── terrain.tesc   # Tessellation control shader
├── terrain.tese   # Tessellation evaluation shader
└── terrain.frag   # Fragment shader (GLSL 4.0)

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
- Caches programs like regular shader programs

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
- Binds vertex attributes for core profile
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

## Shader Details

### Vertex Shader (terrain.vert)

Minimal processing - passes data to tessellation stage:

```glsl
#version 400 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aTexCoord0;
layout(location = 6) in vec4 aTerrainWeights;

out VS_OUT {
    vec3 position;
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} vs_out;

uniform vec3 chunkWorldOffset;

void main() {
    vs_out.position = aPosition + chunkWorldOffset;
    // ... pass through other attributes
}
```

### Tessellation Control Shader (terrain.tesc)

Calculates tessellation level based on camera distance:

```glsl
#version 400 core

layout(vertices = 3) out;  // Triangle patches

uniform vec3 cameraPos;
uniform float tessMinDistance;  // 100.0
uniform float tessMaxDistance;  // 1000.0
uniform float tessMinLevel;     // 1.0
uniform float tessMaxLevel;     // 16.0

float calcTessLevel(vec3 pos0, vec3 pos1) {
    vec3 edgeMidpoint = (pos0 + pos1) * 0.5;
    float distance = length(edgeMidpoint - cameraPos);
    float t = clamp((distance - tessMinDistance) /
                    (tessMaxDistance - tessMinDistance), 0.0, 1.0);
    return mix(tessMaxLevel, tessMinLevel, t);
}

void main() {
    // Pass through control point data
    tcs_out[gl_InvocationID] = tcs_in[gl_InvocationID];

    if (gl_InvocationID == 0) {
        // Set tessellation levels per edge
        gl_TessLevelOuter[0] = calcTessLevel(p1, p2);
        gl_TessLevelOuter[1] = calcTessLevel(p2, p0);
        gl_TessLevelOuter[2] = calcTessLevel(p0, p1);
        gl_TessLevelInner[0] = average(outer levels);
    }
}
```

### Tessellation Evaluation Shader (terrain.tese)

Generates new vertices and applies displacement:

```glsl
#version 400 core

layout(triangles, equal_spacing, ccw) in;

uniform sampler2D snowDeformationMap;
uniform vec3 snowRTTWorldOrigin;
uniform float snowRTTScale;
uniform bool snowDeformationEnabled;

void main() {
    // Interpolate using barycentric coordinates
    vec3 position = gl_TessCoord.x * p0 + gl_TessCoord.y * p1 + gl_TessCoord.z * p2;

    // Apply displacement from deformation map
    if (snowDeformationEnabled && baseLift > 0.01) {
        vec2 deformUV = (position.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
        float deformationFactor = texture(snowDeformationMap, deformUV).r;
        position.z += baseLift * (1.0 - deformationFactor);
    }

    gl_Position = osg_ModelViewProjectionMatrix * vec4(position, 1.0);
}
```

### Fragment Shader (terrain.frag)

Handles lighting and visual effects:

- Parallax Occlusion Mapping (POM) for footprint depth
- Normal perturbation from displacement gradients
- Darkening for compressed snow areas
- Multi-layer terrain texturing
- Fog and depth output

## Usage

### Enabling Tessellation

```cpp
// In terrain initialization
chunkManager->setTessellationEnabled(true);
```

### Configuration Settings (Suggested)

```ini
[Terrain]
; Enable GPU tessellation (requires OpenGL 4.0+)
tessellation = true

; Tessellation LOD parameters
tessellation min distance = 100
tessellation max distance = 1000
tessellation min level = 1
tessellation max level = 16
```

### Runtime Requirements

- OpenGL 4.0+ capable GPU
- Driver support for tessellation shaders
- Graceful fallback to CPU subdivision if unavailable

## Tessellation Parameters

| Uniform | Type | Default | Description |
|---------|------|---------|-------------|
| `cameraPos` | vec3 | viewPoint | Camera position for LOD calculation |
| `tessMinDistance` | float | 100.0 | Distance for maximum tessellation |
| `tessMaxDistance` | float | 1000.0 | Distance for minimum tessellation |
| `tessMinLevel` | float | 1.0 | Minimum subdivision level |
| `tessMaxLevel` | float | 16.0 | Maximum subdivision level |

## Comparison: CPU Subdivision vs GPU Tessellation

| Aspect | CPU Subdivision | GPU Tessellation |
|--------|-----------------|------------------|
| **Processing** | CPU-bound | GPU-bound |
| **Memory** | High (cached geometry) | Low (base mesh only) |
| **LOD Transitions** | Discrete levels | Smooth, continuous |
| **Chunk Boundaries** | Potential seams | Seamless (per-edge LOD) |
| **Compatibility** | All GPUs | OpenGL 4.0+ only |
| **Implementation** | TerrainSubdivider class | Tessellation shaders |

## Implementation Status

### ✓ Dynamic Camera Updates (IMPLEMENTED)

The `cameraPos` uniform is now dynamically updated each frame in `TerrainDrawable::cull()`:

```cpp
// In terraindrawable.cpp
void TerrainDrawable::cull(osgUtil::CullVisitor* cv) {
    // ...
    if (stateset) {
        osg::Uniform* cameraPosUniform = stateset->getUniform("cameraPos");
        if (cameraPosUniform) {
            osg::Vec3f eyePoint = cv->getEyePoint();
            cameraPosUniform->set(eyePoint);
        }
    }
    // ...
}
```

### ✓ Settings Integration (IMPLEMENTED)

Tessellation can be toggled in the Options menu and configured via settings:

```ini
[Terrain]
tessellation = true
tessellation min distance = 50.0
tessellation max distance = 500.0
tessellation min level = 1.0
tessellation max level = 16.0
```

## Future Improvements

### 1. Adaptive Tessellation

Consider screen-space error metrics instead of pure distance:

```glsl
float calcTessLevel(vec3 pos0, vec3 pos1) {
    // Project edge to screen space
    vec4 clip0 = mvp * vec4(pos0, 1.0);
    vec4 clip1 = mvp * vec4(pos1, 1.0);
    vec2 screen0 = clip0.xy / clip0.w * screenSize;
    vec2 screen1 = clip1.xy / clip1.w * screenSize;
    float screenLength = length(screen1 - screen0);

    // Target pixels per edge segment
    return clamp(screenLength / targetPixels, minLevel, maxLevel);
}
```

### 3. Displacement Map Improvements

- Use higher resolution deformation maps
- Implement displacement map mipmapping
- Add edge displacement for chunk boundary continuity

### 4. Performance Optimization

- Profile tessellation overhead
- Consider geometry shader for wireframe debugging
- Implement tessellation LOD culling (skip tessellation for distant chunks)

## Debugging

### Wireframe Mode

To visualize tessellation levels, add to fragment shader:

```glsl
// Debug: color by tessellation level
vec3 debugColor = vec3(gl_TessLevelInner[0] / 16.0, 0.0, 1.0 - gl_TessLevelInner[0] / 16.0);
fragColor.rgb = mix(fragColor.rgb, debugColor, 0.5);
```

### Common Issues

1. **Black terrain**: Check shader compilation errors in log
2. **No tessellation visible**: Verify GL_PATCHES primitive mode is set
3. **Cracks at boundaries**: Ensure matching tessellation levels at edges
4. **Performance issues**: Lower `tessMaxLevel` or increase `tessMinDistance`

## References

- [OpenGL Tessellation Wiki](https://www.khronos.org/opengl/wiki/Tessellation)
- [OpenSceneGraph osg::PatchParameter](http://www.openscenegraph.org/documentation/OpenSceneGraphReferenceDocs/a00607.html)
- [GPU Gems 2: Terrain Rendering](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
