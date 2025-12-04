# Ocean Shader Debug Log

## Debug Results

| Test | Location | Result |
|------|----------|--------|
| Red output | Line 474 | WORKS |
| Refraction before absorption | Line 477 | WORKS |
| Refraction after absorption | Line 501 | WORKS |
| refrReflColor (fresnel blend) | Line 533 | WORKS |
| Full shader without fog | - | WORKS |
| Full shader with fog | - | BLACK |

## Current Issues

1. **FOG causes black screen** - needs investigation
2. **Specular is noisy** - looks like white noise on waves
3. **Shallow water too transparent** - needs wobbly shores

## ogwatershader Analysis

The ogwatershader/water.frag is identical to compatibility/water.frag.

Key features we're missing:
- **Wobbly shores** (lines 227-238): Blends to raw refraction at shallow depth
  - Uses `rawRefraction` stored before absorption
  - `shoreOffset` based on depth and normal wobble
  - Prevents shallow water from looking too processed

Specular formula is identical - so the noise might be from:
- Our FFT normals being too high frequency
- Distance-based normal falloff not working correctly
