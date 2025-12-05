# Ocean Shader Status

## Working
- Refraction with depth absorption
- Reflection with fresnel blending
- Specular highlights (reduced bumpiness for FFT normals)
- Sunlight scattering
- Distance-based normal falloff

## TODO

### High Priority
- [ ] **Fog causes black screen** - skyBlending needs sky texture bound to ocean stateset
  - Workaround: fog disabled in shader, skyBlending hardcoded to "0" in ocean.cpp
- [ ] **Shallow water too transparent** - needs wobbly shores or minimum opacity

### Medium Priority
- [ ] Fine-tune specular - may still be noisy in some conditions
- [ ] Fine-tune absorption curve for better depth appearance

### Low Priority
- [ ] Enable sky blending (bind sky texture properly)
- [ ] Rain ripples support

## Lua Console Commands

```lua
local ocean = require('openmw.ocean')

-- Wind (regenerates spectrum)
ocean.setWindSpeed(20.0)        -- m/s, default 20
ocean.setWindDirection(0.0)     -- degrees, default 0

-- Colors (instant)
ocean.setWaterColor(0.15, 0.25, 0.35)  -- RGB 0-1
ocean.setFoamColor(1.0, 1.0, 1.0)      -- RGB 0-1

-- Wave physics (regenerates spectrum)
ocean.setFetchLength(550000.0)  -- meters, default 550km
ocean.setSwell(0.8)             -- 0-1
ocean.setDetail(1.0)
ocean.setSpread(0.2)            -- 0-1
ocean.setFoamAmount(5.0)

-- Shore smoothing - fragment shader (depth-based, automatic for normals)
ocean.setShoreWaveAttenuation(0.8)  -- 0-1, how much wave normals calm at shore (0=none, 1=full)
ocean.setShoreDepthScale(500.0)     -- MW units, depth at which waves reach full amplitude
ocean.setShoreFoamBoost(1.5)        -- 0-5, extra foam intensity at shore

-- Shore smoothing - vertex shader (manual global control for wave GEOMETRY)
-- This controls the actual physical displacement of the water mesh
ocean.setVertexShoreSmoothing(0.5)  -- 0=full waves, 1=flat water (try 0.3-0.7 for calm ocean)

-- Debug visualization
ocean.setDebugShore(true)  -- Show shore depth: Green=deep(waves), Red=shallow(calm), Blue=raw depth
```

## Key Fixes Applied
1. `worldPos` used for fog distance (not `position.xyz` which was local vertex)
2. Specular bumpiness reduced to 30% with distance falloff (FFT normals are detailed)
3. skyBlending disabled until sky texture is bound
