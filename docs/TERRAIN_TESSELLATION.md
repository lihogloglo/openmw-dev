# GPU Tessellation for OpenMW Terrain

## Overview

This document describes the GPU tessellation system implemented for OpenMW terrain. The tessellation system uses OpenGL 4.0+ hardware tessellation to dynamically subdivide terrain geometry based on camera distance, and optionally applies heightmap displacement from normal map alpha channels.

**Tessellation Type: Quad-based (Quadgrid)**

The system uses quad tessellation rather than triangle tessellation, which is more natural for heightmap-based terrain since terrain grids are organized as quads.

## Features

1. **Distance-based tessellation** - Terrain near the player is subdivided more than distant terrain
2. **Heightmap displacement** - Uses normal map alpha channel to displace vertices for true 3D terrain detail
3. **Togglable** - Both features can be enabled/disabled via settings
4. **Graceful fallback** - Falls back to regular shaders if tessellation is unsupported

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
│Evaluation Shader│  samples displacement map, applies heightmap displacement
└────────┬────────┘
         ▼
┌─────────────────┐
│ Fragment Shader │  terrain.frag - Lighting, texturing
└─────────────────┘
```

### Displacement Map Pipeline

For heightmap displacement, a pre-blended displacement map is rendered per chunk:

```
┌─────────────────────────────┐
│  DisplacementMapRenderer    │  RTT renders blended heights
└──────────────┬──────────────┘
               ▼
┌─────────────────────────────┐
│  Displacement Map Texture   │  RG16F format:
│  (per chunk)                │  R = weighted height sum
│                             │  G = weight sum
└──────────────┬──────────────┘
               ▼
┌─────────────────────────────┐
│  Tessellation Eval Shader   │  Samples texture, computes R/G
│                             │  for final blended height
└─────────────────────────────┘
```

### File Structure

```
files/shaders/core/
├── terrain.vert        # Vertex shader (GLSL 4.0 compatibility)
├── terrain.tesc        # Tessellation control shader
├── terrain.tese        # Tessellation evaluation shader
├── terrain.frag        # Fragment shader
├── displacementmap.vert # Displacement map RTT vertex shader
└── displacementmap.frag # Displacement map RTT fragment shader

components/shader/
├── shadermanager.hpp   # Added getTessellationProgram()
└── shadermanager.cpp   # Implementation

components/terrain/
├── buffercache.hpp     # Added getPatchIndexBuffer()
├── buffercache.cpp     # GL_PATCHES primitive support
├── material.hpp        # Added createTessellationPasses(), createDisplacementMapPasses()
├── material.cpp        # Pass creation
├── chunkmanager.hpp    # Tessellation integration
├── chunkmanager.cpp    # Chunk creation with tessellation
├── displacementmaprenderer.hpp  # Displacement map RTT renderer
├── displacementmaprenderer.cpp  # Implementation
├── terraindrawable.hpp # Displacement map storage
└── terraindrawable.cpp # Dynamic uniform updates
```

---

## CRITICAL: OpenSceneGraph Tessellation Requirements

### GLSL Profile: Use Compatibility, NOT Core

**OpenMW uses GLSL compatibility profile throughout the codebase.** All shaders use built-in variables like `gl_Vertex`, `gl_Normal`, `gl_ModelViewMatrix`, etc.

**WRONG - Core Profile (will NOT work in OpenMW):**
```glsl
#version 400 core
layout(location = 0) in vec3 osg_Vertex;
uniform mat4 osg_ModelViewMatrix;  // NOT provided by OSG without aliasing!
```

**CORRECT - Compatibility Profile:**
```glsl
#version 400 compatibility
void main() {
    vs_out.position = gl_Vertex.xyz;
    vs_out.normal = gl_Normal;
    vs_out.color = gl_Color;
    vs_out.texCoord = gl_MultiTexCoord0.xy;
}
```

### CRITICAL: Reverse-Z Depth Buffer - Use projectionMatrix Uniform

**OpenMW uses a reverse-Z depth buffer** for improved depth precision. This requires a custom projection matrix that is NOT stored in `gl_ProjectionMatrix`.

**WRONG:**
```glsl
gl_Position = gl_ProjectionMatrix * viewPos;  // BROKEN - wrong depth values!
```

**CORRECT:**
```glsl
uniform mat4 projectionMatrix;  // Custom uniform set by OpenMW
gl_Position = projectionMatrix * viewPos;  // Correct reverse-Z depth
```

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

### 2. Buffer Cache Extensions

**New Method: `getPatchIndexBuffer()`**

- Creates index buffers with `GL_PATCHES` primitive mode
- Quad patches (4 vertices per patch)
- Cached separately from triangle index buffers

### 3. Displacement Map System

The displacement map pre-blends all texture layer heights:

1. Each terrain layer's normal map alpha channel contains height data
2. `DisplacementMapRenderer` renders each layer weighted by its blend map
3. Output is RG16F: R = weighted height sum, G = weight sum
4. TES computes final height as R/G (weighted average)

This ensures all tessellation passes displace identically.

---

## Shader Details

### Tessellation Control Shader (terrain.tesc)

Calculates tessellation level based on camera distance:

```glsl
layout(vertices = 4) out;  // Quad patches

uniform vec3 cameraPos;           // Camera in local/model space
uniform float tessMinDistance;    // Distance for max tessellation
uniform float tessMaxDistance;    // Distance for min tessellation
uniform float tessMinLevel;       // Min subdivision (1.0)
uniform float tessMaxLevel;       // Max subdivision (16.0)

float calcTessLevel(vec3 localPos0, vec3 localPos1) {
    vec3 edgeMidpoint = (localPos0 + localPos1) * 0.5;
    float dist = length(edgeMidpoint - cameraPos);
    float t = clamp((dist - tessMinDistance) /
                    (tessMaxDistance - tessMinDistance), 0.0, 1.0);
    return mix(tessMaxLevel, tessMinLevel, t);
}
```

### Tessellation Evaluation Shader (terrain.tese)

Generates new vertices with bilinear interpolation and applies displacement:

```glsl
layout(quads, equal_spacing, ccw) in;

uniform sampler2D displacementMap;
uniform bool heightmapDisplacementEnabled;
uniform float heightmapDisplacementStrength;

void main() {
    // Bilinear interpolation for quads
    vec3 position = interpolate3(...);

    if (heightmapDisplacementEnabled) {
        vec2 dispSample = texture(displacementMap, texCoord).rg;
        float height = (dispSample.g > 0.001) ? (dispSample.r / dispSample.g) : 0.5;
        float displacement = (height - 0.5) * heightmapDisplacementStrength;

        // Distance-based falloff
        float falloff = 1.0 - smoothstep(tessMinDistance, tessMaxDistance, distToCamera);
        displacement *= falloff;

        position += normal * displacement;
    }

    vec4 viewPos = gl_ModelViewMatrix * vec4(position, 1.0);
    gl_Position = projectionMatrix * viewPos;
}
```

---

## Configuration

### Settings

```ini
[Terrain]
# Enable GPU tessellation (requires OpenGL 4.0+)
tessellation = true

# Tessellation LOD parameters
tessellation min distance = 500.0
tessellation max distance = 2000.0
tessellation min level = 1.0
tessellation max level = 32.0

# Heightmap displacement from normal map alpha
heightmap displacement = false
heightmap displacement strength = 10.0
```

### Parameters

| Setting | Default | Description |
|---------|---------|-------------|
| `tessellation` | true | Enable/disable tessellation |
| `tessellation min distance` | 500.0 | Distance for maximum tessellation |
| `tessellation max distance` | 2000.0 | Distance for minimum tessellation |
| `tessellation min level` | 1.0 | Minimum subdivision level |
| `tessellation max level` | 32.0 | Maximum subdivision level |
| `heightmap displacement` | false | Enable displacement from normal maps |
| `heightmap displacement strength` | 10.0 | Displacement magnitude in game units |

---

## Known Issues and Solutions

### Issue: Composite Map Chunks

Composite map chunks (large/distant terrain) use pre-baked textures and don't need tessellation. The system automatically uses regular triangles for these:

```cpp
bool useCompositeMap = chunkSize >= mCompositeMapLevel;
bool useTessellation = !useCompositeMap && Settings::terrain().mTessellation.get();
```

### Issue: Edge Seams

Per-chunk displacement maps may have slightly different values at edges. A 2% edge fade is applied to hide seams:

```glsl
float edgeMargin = 0.02;
float edgeFadeX = smoothstep(0.0, edgeMargin, texCoord.x) *
                  smoothstep(0.0, edgeMargin, 1.0 - texCoord.x);
displacement *= edgeFadeX * edgeFadeY;
```

---

## Runtime Requirements

- OpenGL 4.0+ capable GPU
- Driver support for tessellation shaders
- Automatic fallback to regular shaders if unavailable

---

## References

- [OpenGL Tessellation Wiki](https://www.khronos.org/opengl/wiki/Tessellation)
- [OSG Tessellation Example](https://github.com/openscenegraph/OpenSceneGraph/blob/master/examples/osgtessellationshaders/osgtessellationshaders.cpp)
- [GPU Gems 2: Terrain Rendering](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
