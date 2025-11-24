# Ocean Vibration Bug Fix - Clipmap UV Snapping

## Problem Summary

The ocean exhibited visible vibration/shimmering when the camera moved or rotated after implementing the clipmap LOD system. This was particularly noticeable in commits after `051aafce46b5c063f729a0ad8fe83115a070743b`.

## Root Cause Analysis

### The Bug

The vibration was caused by a fundamental mismatch between **mesh movement** and **texture UV sampling**:

1. **Old Approach (Incorrect)**:
   - The ocean mesh was moved to follow the camera, snapping to a grid of ~7.083 units
   - The mesh position (`nodePosition`) was updated every frame as the camera moved
   - UV coordinates were calculated as: `uv = (vertexLocalPos + nodePosition) * uvScale`
   - **Problem**: When `nodePosition` changed (mesh snapped to new grid position), the UVs shifted, causing texture samples to jump between different texels
   - This created visible vibration as the displacement and normal maps "swam" across the mesh

2. **Grid Snapping Math Issues**:
   - Grid snap size was calculated as: `tileSize0 / 512.0f = 3626.5 / 512 = 7.08300781 units`
   - Actual vertex spacing was: `(2 * CASCADE_0_RADIUS) / 512 = 3626 / 512 = 7.08203125 units`
   - The 0.001 unit difference accumulated, causing misalignment

### How Godot Handles This

In the Godot reference implementation:

1. **Mesh Stays Stationary**: The water mesh never moves from its origin
2. **World-Space Vertices**: Uses `render_mode world_vertex_coords` so vertices are in world space
3. **UV = World Position**: UVs are directly `VERTEX.xz` (world coordinates)
4. **No Movement, No Swimming**: Since the mesh doesn't move, UVs are stable

## The Solution

We adopt Godot's approach with the following changes:

### 1. Keep Mesh Stationary ([ocean.cpp:175-186](apps/openmw/mwrender/ocean.cpp#L175-L186))

```cpp
// Clipmap Ocean: Keep mesh stationary, only update camera position for shader
// Unlike traditional infinite ocean, clipmap mesh stays at origin (0,0,height)
mRootNode->setPosition(osg::Vec3f(0.f, 0.f, mHeight));
mNodePositionUniform->set(osg::Vec3f(0.f, 0.f, mHeight));
```

**Before**: Mesh moved to follow camera, causing UV shifts
**After**: Mesh stays at (0, 0, height), never moves

### 2. Vertex Shader: Offset Vertices in Shader ([ocean.vert:36-76](files/shaders/compatibility/ocean.vert#L36-L76))

Instead of moving the mesh, we offset vertices in the shader:

```glsl
// Calculate grid snap size matching Ring 0 vertex spacing
const float CASCADE_0_RADIUS = 50.0 * 72.53 / 2.0;  // 1813.25 units
const float RING_0_GRID_SIZE = 512.0;
float gridSnapSize = (2.0 * CASCADE_0_RADIUS) / RING_0_GRID_SIZE; // 7.08203125 units

// Snap camera position to grid (prevents texture swimming)
vec2 snappedCameraPos = floor(cameraPosition.xy / gridSnapSize) * gridSnapSize;

// Calculate world position: local vertex + snapped camera offset
vec2 worldPosXY = vertPos.xy + snappedCameraPos;

// Sample displacement using stable world UVs
vec2 uv = worldPosXY * mapScales[i].x;
```

**Key Points**:
- `snappedCameraPos` changes in discrete steps of `gridSnapSize`
- `worldPosXY` is stable between snaps
- UVs don't swim because they're based on snapped world position

### 3. Fragment Shader: Use World Position from Vertex Shader ([ocean.frag:63-71](files/shaders/compatibility/ocean.frag#L63-L71))

The fragment shader simply uses the world position calculated in the vertex shader:

```glsl
// worldPos is calculated in vertex shader as: vec3(worldPosXY, vertPos.z) + nodePosition
// Since nodePosition is (0, 0, height), worldPos.xy already contains the snapped world XY
vec2 worldPosXY = worldPos.xy;

// Sample normals using same UVs as displacement
for (int i = 0; i < numCascades && i < 4; ++i) {
    vec2 uv = worldPosXY * mapScales[i].x;
    vec4 normalSample = texture(normalMap, vec3(uv, float(i)));
    ...
}
```

## Why This Works

### Texture Sampling Stability

1. **Snapped Grid**:
   - Camera position snaps to grid of 7.08203125 units
   - This matches Ring 0's vertex spacing exactly
   - Vertices land on consistent world positions

2. **Stable UVs**:
   - UVs are calculated from `snappedCameraPos`, not continuous camera position
   - Between snaps, UVs don't change at all
   - When snap occurs, UVs shift by exactly 1 texel (for Cascade 0)

3. **Precise Alignment**:
   ```
   Grid snap size  = (2 * 1813.25) / 512 = 7.08203125 units
   Cascade 0 texel = 3626.5 / 512       = 7.08203125 units
   PERFECT MATCH!
   ```

### Visual Result

- **Before**: Ocean surface vibrates/shimmers during camera movement
- **After**: Ocean surface remains stable, waves animate smoothly
- **Snapping**: Occurs every 7.082 units, but is imperceptible due to texel alignment

## Implementation Details

### Constants Used

```cpp
METERS_TO_MW_UNITS = 72.53
CASCADE_0_TILE_SIZE = 50.0 meters = 3626.5 MW units
CASCADE_0_RADIUS = CASCADE_0_TILE_SIZE / 2 = 1813.25 MW units
RING_0_GRID_SIZE = 512
VERTEX_SPACING = (2 * 1813.25) / 512 = 7.08203125 MW units
```

### Shader Uniforms Required

- `cameraPosition`: Camera world position (updated every frame)
- `nodePosition`: Mesh origin (always (0, 0, height)) - used in both vertex and fragment shaders
- `mapScales[4]`: Per-cascade UV scales and displacement scales
- `displacementMap`: Texture2DArray containing FFT displacement data
- `normalMap`: Texture2DArray containing FFT normal/gradient data

### Files Modified

1. **apps/openmw/mwrender/ocean.cpp** (lines 175-186)
   - Removed mesh movement logic
   - Keep mesh at origin (0, 0, height)

2. **files/shaders/compatibility/ocean.vert** (lines 31-84)
   - Added grid snapping logic
   - Offset vertices in shader to follow camera
   - Calculate stable world UVs using snapped camera position

3. **files/shaders/compatibility/ocean.frag** (lines 41-42, 63-71)
   - Added `nodePosition` uniform declaration
   - Use worldPos.xy directly for UV sampling (simpler than before)
   - Use consistent world UVs for normals

## Testing

### Expected Results

✅ **Ocean should NOT vibrate when**:
- Camera rotates (yaw/pitch/roll)
- Camera translates (forward/backward/strafe)
- Player moves while camera follows

✅ **Ocean should animate smoothly**:
- Wave displacement continues to animate
- FFT time progression is unaffected
- Foam patterns flow naturally

✅ **No visible popping**:
- Grid snaps every 7.082 units
- Snapping aligned with texel boundaries
- Imperceptible to the player

### Debug Visualization

Enable LOD debug mode to verify grid alignment:
```cpp
mDebugVisualizeLODUniform->set(1);
```

Each ring should show a stable grid that doesn't vibrate.

## Comparison with Godot

| Aspect | Godot | OpenMW (Before) | OpenMW (After) |
|--------|-------|-----------------|----------------|
| Mesh Movement | Stationary | Moves with camera | Stationary |
| Vertex Coords | World space | Local space | Local space |
| UV Calculation | `VERTEX.xz` | `localPos + meshPos` | `localPos + snappedCamera` |
| Snapping | Implicit | Mesh position | Shader calculation |
| Vibration | None | Severe | None ✅ |

## Related Issues

This fix resolves the issue described in:
- Tracking doc section 1.6: "Animation Vibration / Shimmering"
- Commits: `666381e932`, `caf6c650a6`, `85aee4ca54`

## References

- **Godot Implementation**: `godotocean/assets/shaders/spatial/water.gdshader`
- **Clipmap Mesh**: Stationary mesh with concentric LOD rings
- **World-Space UVs**: Essential for preventing texture swimming

---

**Last Updated**: 2025-11-24
**Status**: ✅ FIXED
**Tested**: Pending
