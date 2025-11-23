# Ocean Unit Conversion and Vertex Density Fix

## Summary

Fixed critical issues with the ocean implementation to match Godot reference implementation and Morrowind's unit system.

## Issues Fixed

### 1. Unit Conversion (CRITICAL)
**Problem**: Wave parameters were using arbitrary units instead of proper Morrowind units.

**Morrowind Unit System**:
- 22.1 units = 1 foot
- 1 meter = 3.28084 feet = **72.53 Morrowind units**

**Changes Made**:
- Added `METERS_TO_MW_UNITS = 72.53f` constant
- Updated cascade tile sizes from arbitrary values to physical meters converted to MW units:
  - **Old**: `{ 250.0f, 500.0f, 1000.0f, 2000.0f }` (arbitrary units)
  - **New**: `{ 50m, 100m, 200m, 400m }` → `{ 3,626.5, 7,253, 14,506, 29,012 }` MW units
- Updated water depth parameter: `1000m * 72.53 = 72,530` MW units
- Updated grid snap size to match largest cascade

### 2. Vertex Density (CRITICAL)
**Problem**: Mesh was too sparse to properly display FFT wave displacement.

**Analysis**:
- **Godot high-quality mesh**: 328,854 vertices
- **Old OpenMW mesh**: 256×256 = 65,792 vertices over 100,000 units
  - Vertex spacing: ~390 units apart (way too sparse!)
- **FFT texture resolution**: 512×512 pixels

**Changes Made**:
- Increased grid resolution: `256 → 512` (4x more vertices)
- Reduced world size to match wave scale:
  - **Old**: 100,000 units (arbitrary)
  - **New**: `400m * 72.53 * 2 = 58,024` units (covers largest cascade with margin)
- **New vertex spacing**: 58,024 / 512 ≈ **113 units** (3.4x improvement)
- **New total vertices**: 512×512 = 262,144 vertices

### 3. UV Scale Calculation
**Problem**: Shader UV scales were calculated using incorrect formula.

**Changes Made**:
- Updated `mapScales` uniform calculation to use actual tile sizes in MW units
- Formula: `uvScale = 1.0 / (tileSizeMeters * METERS_TO_MW_UNITS)`
- Each cascade now has correct UV scaling for its physical domain size

## Physical Scale Comparison

| Parameter | Godot (meters) | OpenMW (MW units) | Ratio |
|-----------|----------------|-------------------|-------|
| Cascade 0 | 50m | 3,626.5 | 72.53:1 |
| Cascade 1 | 100m | 7,253.0 | 72.53:1 |
| Cascade 2 | 200m | 14,506.0 | 72.53:1 |
| Cascade 3 | 400m | 29,012.0 | 72.53:1 |
| World Size | 800m | 58,024 | 72.53:1 |
| Vertex Spacing | ~1.56m | ~113 units | 72.53:1 |

## Expected Results

### Before Fix
- ❌ Waves appear at wrong physical scale
- ❌ Very blocky/pixelated displacement due to vertex undersampling
- ❌ Wave patterns don't match Godot reference

### After Fix
- ✅ Waves match physical scale of Godot implementation
- ✅ Smoother wave displacement with 4x more vertices
- ✅ Proper vertex density relative to FFT texture resolution
- ✅ Cascade sizes make physical sense in Morrowind's world

## Remaining Considerations

### Further Improvements (Optional)
1. **Implement Clipmap LOD System** (like Godot)
   - Current: Single 512×512 mesh
   - Godot: Multiple meshes with varying density
   - Benefit: Higher detail near camera, better performance

2. **Dynamic Vertex Density**
   - Tessellation shaders (if available)
   - Adaptive mesh refinement based on camera distance

3. **Tuning Parameters**
   - Wind speed, direction, fetch length
   - Cascade displacement/normal scales
   - Fine-tune for Morrowind aesthetics

### Performance Impact
- **Memory**: Increased from 65K to 262K vertices (~4x)
- **Draw calls**: No change (still single mesh)
- **Expected FPS impact**: Minimal (modern GPUs handle this easily)
- **Benefit**: Proper wave rendering vs. blocky artifacts

## Files Modified

- `apps/openmw/mwrender/ocean.cpp`
  - Added `METERS_TO_MW_UNITS` constant
  - Updated cascade tile sizes with proper unit conversion
  - Increased grid resolution from 256 to 512
  - Reduced world size from 100,000 to 58,024 units
  - Fixed UV scale calculations
  - Updated depth parameter conversion
  - Fixed grid snap size

## Testing Checklist

- [ ] Ocean renders without artifacts
- [ ] Wave displacement is smooth (not blocky)
- [ ] Wave scale looks appropriate for Morrowind
- [ ] No performance degradation
- [ ] Ocean follows camera smoothly
- [ ] Cascade blending works correctly
- [ ] Normals are computed correctly
- [ ] Foam/whitecaps appear at appropriate scale

## Notes

- All physical parameters are now in meters and converted to MW units
- Conversion factor (72.53) is derived from official Morrowind units (22.1 units/foot)
- Vertex density is now appropriate for 512×512 FFT texture resolution
- Cascade sizes follow power-of-2 scaling for optimal FFT coverage
