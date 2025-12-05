# Rain Ripple Occlusion
Made with Claude AI (Opus 4.5).

From this issue : https://gitlab.com/OpenMW/openmw/-/issues/7157  
Prior to this implementation, rain ripples would appear on all water surfaces during rainy weather, regardless of whether the water was actually exposed to rain.

The rain ripple occlusion system reuses the existing precipitation particle occlusion infrastructure. This system renders a depth map from an overhead orthographic camera, which is then sampled in the water shader to determine whether each water fragment is sheltered from rain.

## Implementation Details

The precipitation occlusion system (`PrecipitationOccluder` class in `precipitationocclusion.cpp`) provides:

1. **Depth Texture**: A 256x256 depth texture rendered from above the player
2. **Orthographic Camera**: Looks straight down, covering the area where rain particles spawn
3. **View-Projection Matrix**: Transforms world positions to the occlusion camera's clip space

The occlusion camera renders scene geometry (static objects, terrain) to capture what structures exist above the player's area.

#### Vertex Shader (`water.vert`)

The vertex shader transforms each water vertex position into the occlusion camera's clip space:

```glsl
vec4 occlusionClipPos = rainOcclusionMatrix * vec4(worldPos, 1.0);
```

The clip coordinates are then converted to texture coordinates for sampling the depth map. The Z coordinate handling differs based on depth buffer mode:

- **Reversed-Z** (`GL_ZERO_TO_ONE`): NDC z is already in [0,1] range, used directly
- **Standard Z**: NDC z is in [-1,1] range, transformed via `z * 0.5 + 0.5`

```glsl
#if @reverseZ
    rainOcclusionCoord = vec3(occlusionClipPos.xy * 0.5 + 0.5, occlusionClipPos.z);
#else
    rainOcclusionCoord = occlusionClipPos.xyz * 0.5 + 0.5;
#endif
```

#### Fragment Shader (`water.frag`)

The fragment shader samples the occlusion depth map and compares it with the water fragment's depth:

```glsl
float sceneDepth = texture2D(rainOcclusionMap, rainOcclusionCoord.xy).r;
#if @reverseZ
    occlusionFactor = (rainOcclusionCoord.z < sceneDepth) ? 0.0 : 1.0;
#else
    occlusionFactor = (rainOcclusionCoord.z > sceneDepth) ? 0.0 : 1.0;
#endif
```

The depth comparison determines if there's geometry between the sky (rain source) and the water surface:

- **Reversed-Z**: Near objects have higher depth values. Water is occluded if its depth is less than scene depth (something is closer to the sky camera)
- **Standard Z**: Near objects have lower depth values. Water is occluded if its depth is greater than scene depth

The `occlusionFactor` (0.0 = occluded, 1.0 = exposed) is multiplied with the rain ripple effect intensity.

### Data Flow

1. `SkyManager` creates and manages the `PrecipitationOccluder`
2. `RenderingManager::update()` calls `SkyManager::updatePrecipitationOccluder()` to update camera matrices
3. The occlusion texture and matrix are passed to `Water::setRainRippleOcclusion()`
4. `ShaderWaterStateSetUpdater` binds the texture and matrix uniforms for the water shader
5. The water shader samples the occlusion map and modulates rain ripple intensity

## Limitations

### Coverage Area

The occlusion system only covers a limited area around the player (approximately matching the rain particle spawn area, ~1000x1000 units). Water surfaces outside this area will not have occlusion applied and will show ripples regardless of overhead structures.

### Performance Considerations

The occlusion depth pass renders scene geometry from an additional camera. The cost is mitigated by:
- Small texture size (256x256)
- Small feature culling to skip tiny objects
- Reusing the same depth texture already rendered for particle occlusion

## Settings

The feature is controlled by the `weather particle occlusion` setting in the `[Shaders]` section:

```ini
[Shaders]
weather particle occlusion = true
```
