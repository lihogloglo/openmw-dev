# OpenMW Snow Deformation System

## Overview

This implements real-time 3D terrain deformation for continuous trails in snow (and can be extended to sand/ash/mud). The system uses **Vertex Texture Fetch (VTF)** with dense meshes instead of hardware tessellation, making it compatible with OpenGL 3.0+ and OpenMW's existing terrain architecture.

## Key Features

- ✅ **Continuous smooth trails** (not discrete footprints)
- ✅ **Real-time vertex displacement** via VTF sampling
- ✅ **Dynamic normal recalculation** using finite differences
- ✅ **Automatic decay/settling** over time
- ✅ **Distance-based LOD** (dense mesh only near player)
- ✅ **Clean integration** with OpenMW's terrain system
- ✅ **Performance optimized** (1024×1024 deformation texture, 128×128 mesh grid)

---

## Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                    RenderingManager                          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │           SnowDeformationManager                      │  │
│  │                                                       │  │
│  │  ┌────────────────┐      ┌────────────────────────┐ │  │
│  │  │ Footprint      │─────▶│ DeformationTexture     │ │  │
│  │  │ Tracker        │      │ (1024×1024 RTT)        │ │  │
│  │  └────────────────┘      └────────────────────────┘ │  │
│  │          │                          │                │  │
│  │          │                          ▼                │  │
│  │          │               ┌─────────────────────┐    │  │
│  │          └──────────────▶│  Dense Mesh         │    │  │
│  │                          │  (128×128 vertices) │    │  │
│  │                          │  + VTF Shader       │    │  │
│  │                          └─────────────────────┘    │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Component Details

#### 1. **SnowDeformationManager**
- **Location**: `apps/openmw/mwrender/snowdeformation.{hpp,cpp}`
- **Purpose**: Central coordinator for deformation system
- **Responsibilities**:
  - Tracks player movement and generates footprints
  - Updates deformation texture via RTT
  - Manages dense mesh positioning around player
  - Handles decay/settling over time

#### 2. **DeformationMeshGenerator**
- **Purpose**: Creates dense overlay mesh
- **Details**:
  - Generates 128×128 vertex grid (16,384 vertices)
  - Covers 4×4 world units (2 unit radius from player)
  - Uses VBOs for performance
  - UVs map to deformation texture

#### 3. **DeformationTextureRenderer**
- **Purpose**: Render-to-texture system for deformation heightmap
- **Details**:
  - 1024×1024 R16F texture (single-channel 16-bit float)
  - Covers 8×8 world units
  - Scrolls/follows player movement
  - Renders footprints as radial gradients

#### 4. **Shaders**
- **snow_deformation.vert**: VTF vertex displacement + normal recalculation
- **snow_deformation.frag**: Snow material rendering with lighting
- **snow_footprint.{vert,frag}**: Footprint rendering to deformation texture
- **snow_decay.frag**: Decay shader for gradual settling
- **snow_fullscreen.vert**: Simple passthrough for fullscreen quads

---

## Technical Details

### Vertex Texture Fetch (VTF) Approach

The vertex shader samples the deformation texture and displaces vertices downward:

```glsl
// Sample deformation at world position
float deformation = sampleDeformation(worldPosXY);

// Displace vertex downward (negative Z)
float displacement = -deformation * deformationStrength * depthMultiplier;
vec3 displacedVertex = osg_Vertex + vec3(0.0, 0.0, displacement);
```

### Normal Recalculation

Normals are recalculated using **finite differences** (gradient sampling):

```glsl
// Sample neighboring heights
float heightRight = sampleDeformation(worldPos + vec2(delta, 0.0));
float heightLeft = sampleDeformation(worldPos - vec2(delta, 0.0));
float heightUp = sampleDeformation(worldPos + vec2(0.0, delta));
float heightDown = sampleDeformation(worldPos - vec2(0.0, delta));

// Calculate gradients
float dx = (heightRight - heightLeft) / (2.0 * delta);
float dy = (heightUp - heightDown) / (2.0 * delta);

// Construct normal from tangent/bitangent
vec3 tangent = normalize(vec3(1.0, 0.0, dx * deformationStrength));
vec3 bitangent = normalize(vec3(0.0, 1.0, dy * deformationStrength));
vec3 normal = normalize(cross(tangent, bitangent));
```

### Footprint Tracking

- New footprint created every **0.3 world units** of player movement
- Footprints stored with position, intensity, radius, and timestamp
- Intensity decays at **10% per second** (configurable)
- Fully decayed footprints are removed from tracking

### Deformation Texture

- **Size**: 1024×1024 pixels
- **Format**: R16F (16-bit float, red channel only)
- **Coverage**: 8×8 world units
- **Resolution**: ~128 pixels per world unit
- **Scrolling**: Texture center smoothly follows player (90% old + 10% new each frame)

### Dense Mesh

- **Resolution**: 128×128 vertices = 16,384 vertices
- **Size**: 4×4 world units (2 unit radius from player)
- **Vertex Spacing**: ~0.03 world units
- **Triangles**: 32,258 triangles
- **Updates**: Position follows player, transforms updated each frame

---

## Performance Characteristics

### Memory Usage
- **Deformation Texture**: 2 MB (1024×1024 × 2 bytes)
- **Mesh Vertices**: ~262 KB (16,384 × 16 bytes)
- **Footprint Tracking**: ~48 bytes per active footprint (typically <100 active)

### GPU Cost
- **Vertex Shader**: 5 texture lookups per vertex (1 height + 4 neighbors for normals)
- **Fragment Shader**: Standard Blinn-Phong lighting
- **RTT Passes**: 2 per frame (footprint render + decay)

### Typical Performance
- **16,384 vertices** × 5 texture samples = **81,920 texture fetches per frame**
- With modern GPUs (even integrated), this is negligible
- Mesh only active within 2 units of player (culled otherwise)

---

## Configuration Parameters

### SnowDeformationManager Settings

```cpp
// Deformation radius (world units from player)
manager->setDeformationRadius(2.0f);  // Default: 2.0

// Deformation strength multiplier
manager->setDeformationStrength(1.0f);  // Default: 1.0 (0.0-2.0 recommended)

// Enable/disable system
manager->setEnabled(true);
```

### Tweakable Constants (in snowdeformation.cpp)

```cpp
static constexpr int DEFORMATION_TEXTURE_SIZE = 1024;
static constexpr float DEFAULT_DEFORMATION_RADIUS = 2.0f;
static constexpr float DEFAULT_WORLD_TEXTURE_SIZE = 8.0f;
static constexpr float DEFAULT_FOOTPRINT_INTERVAL = 0.3f;  // Distance between footprints
static constexpr float DEFAULT_DECAY_RATE = 0.1f;          // 10% fade per second
```

### Material-Specific Settings (in deformation.glsl)

```glsl
// Snow: Deep deformation, slow decay
if (materialType == 1) // TERRAIN_SNOW
    return 1.0;  // Full depth
    decayRate = 0.995;  // 0.5% per frame

// Sand: Medium deformation, medium decay
else if (materialType == 2) // TERRAIN_SAND
    return 0.6;  // 60% depth
    decayRate = 0.990;  // 1% per frame

// Ash: Shallow deformation, fast decay
else if (materialType == 3) // TERRAIN_ASH
    return 0.3;  // 30% depth
    decayRate = 0.980;  // 2% per frame
```

---

## Integration Points

### RenderingManager Integration

```cpp
// In RenderingManager constructor
mSnowDeformation = std::make_unique<SnowDeformationManager>(sceneRoot, mResourceSystem);

// In RenderingManager::update()
if (mSnowDeformation)
    mSnowDeformation->update(playerPos, dt);

// Public interface
SnowDeformationManager* getSnowDeformationManager();
void setSnowDeformationEnabled(bool enabled);
```

### Scene Graph Hierarchy

```
mSceneRoot (SceneUtil::LightManager)
└── mDeformationMeshGroup (osg::Group)
    └── mDenseMesh (osg::Geometry) [with VTF shader]
```

The mesh has `Mask_Terrain` visibility, rendering with the terrain pass.

---

## Extending to Other Materials

The system is designed to support **sand**, **ash**, and **mud** with minimal changes:

### Step 1: Detect Terrain Material Type

Add terrain material detection in `SnowDeformationManager::update()`:

```cpp
// Query terrain texture at player position
int materialType = detectTerrainMaterial(playerPos);

if (materialType == TERRAIN_SNOW ||
    materialType == TERRAIN_SAND ||
    materialType == TERRAIN_ASH)
{
    // Enable deformation
    mEnabled = true;
}
else
{
    mEnabled = false;
}
```

### Step 2: Pass Material Type to Shader

```cpp
mDeformationStateSet->addUniform(new osg::Uniform("terrainMaterialType", materialType));
```

### Step 3: Use Material-Specific Parameters

The shader already has material-specific logic via `deformation.glsl`:
- `getDepthMultiplier(materialType)` - How deep to sink
- `getDecayRate(materialType)` - How fast to settle

---

## Debugging and Visualization

### Enable Deformation Texture Visualization

To view the deformation texture in real-time:

```cpp
// In RenderingManager or via console command
osg::Texture2D* defTex = mSnowDeformation->getDeformationTexture();
// Display as HUD overlay for debugging
```

### Mesh Wireframe Mode

```cpp
osg::PolygonMode* polymode = new osg::PolygonMode;
polymode->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE);
mDeformationStateSet->setAttribute(polymode);
```

### Footprint Debug Visualization

Add debug spheres at footprint positions:

```cpp
for (const auto& footprint : mFootprints) {
    // Draw debug sphere at footprint.position with intensity-based color
}
```

---

## Limitations and Future Work

### Current Limitations

1. **No Persistence**: Deformation disappears when player moves far away
2. **Single Texture**: Only one 8×8 unit area is tracked
3. **No Multi-Material**: Currently snow-only (easy to extend)
4. **No Physics**: Deformation is visual only, doesn't affect collision

### Future Enhancements

#### 1. **Deformation Persistence**
- Store deformation in cell data
- Save/load with game state
- Fade out distant deformation gradually

#### 2. **Multi-Texture Scrolling**
- Use texture atlas or virtual texturing
- Track larger area (e.g., 64×64 units)
- Implement texture paging

#### 3. **Material Auto-Detection**
- Query terrain blend maps
- Determine material type automatically
- Blend deformation based on material mix

#### 4. **Weather Integration**
- Fresh snowfall "fills" deformation
- Rain causes faster settling
- Wind affects decay patterns

#### 5. **NPC Footprints**
- Track nearby NPC movement
- Add NPC footprints to deformation texture
- Different footprint sizes per character

#### 6. **Physics Integration**
- Update Bullet heightfield with deformation
- Affects character movement (slower in deep snow)
- Objects sink into deformed terrain

#### 7. **Performance Optimizations**
- Adaptive mesh resolution based on GPU performance
- Texture compression (BC4 for R8 heightmap)
- Instanced rendering for multiple deformation zones

---

## File Structure

```
apps/openmw/mwrender/
├── snowdeformation.hpp          # Main header
├── snowdeformation.cpp          # Implementation
└── renderingmanager.{hpp,cpp}   # Integration

files/shaders/
├── snow_deformation.vert        # VTF + normal recalculation
├── snow_deformation.frag        # Snow material shading
├── snow_footprint.vert          # Footprint rendering vertex
├── snow_footprint.frag          # Footprint rendering fragment
├── snow_decay.frag              # Decay post-process
├── snow_fullscreen.vert         # Fullscreen quad helper
└── lib/terrain/
    └── deformation.glsl         # Material parameter library (existing)

apps/openmw/
└── CMakeLists.txt               # Build configuration (snowdeformation added)
```

---

## Building

The system is integrated into OpenMW's standard build process:

```bash
cd openmw-snow
mkdir build && cd build
cmake ..
cmake --build . --target openmw
```

No additional dependencies required - uses existing OpenMW infrastructure.

---

## Usage

### In-Game Activation

The system is **enabled by default** when initialized. To toggle:

Via C++ code:
```cpp
MWRender::RenderingManager* renderMgr = /* ... */;
renderMgr->setSnowDeformationEnabled(true);
```

Via console (if console command added):
```
setSnowDeformation 1
```

### Testing

1. **Start OpenMW** with this build
2. **Walk around** in snowy areas
3. **Look back** to see continuous trails forming
4. **Wait** to observe gradual decay/settling

---

## Known Issues

None currently. This is a prototype implementation.

---

## Credits

- **Architecture**: Hybrid dense mesh + VTF approach
- **Inspiration**: OpenMW's existing ripples system ([ripples.cpp](apps/openmw/mwrender/ripples.cpp))
- **Shader Library**: Uses existing `deformation.glsl` from OpenMW
- **Integration**: Follows ObjectPaging pattern for chunk management

---

## License

Same as OpenMW (GPL 3.0)
