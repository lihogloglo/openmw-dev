# WCOL (Water Color) Implementation Status

## Overview
Implementation of per-cell water color via WCOL subrecord in .omwaddon files for OpenMW's multi-level water system.

---

## ✅ COMPLETED TASKS

### 1. ESM3 Cell Format Extensions
**Files**: `components/esm3/loadcell.hpp`, `components/esm3/loadcell.cpp`

#### Changes Made:
- **New WCOL Subrecord Format**: 12 bytes (3 × 4-byte float RGB, 0-1 range)
- **Cell Structure Fields Added**:
  ```cpp
  float mWaterColorR{ 0.15f };
  float mWaterColorG{ 0.25f };
  float mWaterColorB{ 0.35f };
  bool mHasWaterColor{ false };
  ```

#### Implementation Details:
- **Loading** (`loadcell.cpp:160-180`):
  - Parses `fourCC("WCOL")` subrecord
  - Reads 3 sequential floats with `esm.getHT()`
  - Validates: finite values, 0-1 range
  - Logs warning and resets to default on invalid data

- **Saving** (`loadcell.cpp:212-234`):
  - Writes WCOL for both interior and exterior cells when `mHasWaterColor == true`
  - Three sequential `esm.writeHNT("WCOL", ...)` calls

- **Initialization** (`loadcell.cpp:340-343`):
  - `blank()` method resets to default values

---

### 2. MWWorld::Cell Runtime Exposure
**Files**: `apps/openmw/mwworld/cell.hpp`, `apps/openmw/mwworld/cell.cpp`

#### Changes Made:
- **Public Getters** (`cell.hpp:50-53`):
  ```cpp
  bool hasWaterColor() const { return mHasWaterColor; }
  float getWaterColorR() const { return mWaterColorR; }
  float getWaterColorG() const { return mWaterColorG; }
  float getWaterColorB() const { return mWaterColorB; }
  ```

- **Member Variables** (`cell.hpp:71-74`):
  ```cpp
  bool mHasWaterColor{ false };
  float mWaterColorR{ 0.15f };
  float mWaterColorG{ 0.25f };
  float mWaterColorB{ 0.35f };
  ```

- **Constructor Initialization** (`cell.cpp:90-93`):
  ```cpp
  , mHasWaterColor(cell.mHasWaterColor)
  , mWaterColorR(cell.mWaterColorR)
  , mWaterColorG(cell.mWaterColorG)
  , mWaterColorB(cell.mWaterColorB)
  ```

---

### 3. Lake Class Per-Cell Color Support
**Files**: `apps/openmw/mwrender/lake.hpp`, `apps/openmw/mwrender/lake.cpp`

#### Changes Made:
- **CellWater Structure** (`lake.hpp:136`):
  ```cpp
  struct CellWater {
      int gridX, gridY;
      float height;
      osg::Vec3f waterColor;  // NEW
      osg::ref_ptr<osg::PositionAttitudeTransform> transform;
      osg::ref_ptr<osg::Geometry> geometry;
  };
  ```

- **Updated Method Signature** (`lake.hpp:98`):
  ```cpp
  void addWaterCell(int gridX, int gridY, float height,
                    const osg::Vec3f& waterColor = osg::Vec3f(0.15f, 0.25f, 0.35f));
  ```

- **New Query Method** (`lake.hpp:102`, `lake.cpp:454-467`):
  ```cpp
  osg::Vec3f getWaterColorAt(const osg::Vec3f& pos) const;
  ```

- **Shader Uniform Setup** (`lake.cpp:510-511`):
  ```cpp
  // Add per-cell water color uniform
  ss->addUniform(new osg::Uniform("waterColor", cell.waterColor));
  ```

- **Logging Enhancement** (`lake.cpp:383`):
  - Added water color RGB values to debug log output

---

### 4. JSON Format Updated
**File**: `MultiLevelWater/lakes.json`

#### Changes Made:
- Uses `gridX`/`gridY` (cell coordinates) instead of world coordinates
- Added `waterColor` array `[R, G, B]` to each lake entry
- Contains 6 sample lakes with different colors:
  - Caldera Mountain Lake: `[0.1, 0.3, 0.4]`
  - Vivec Canals: `[0.15, 0.25, 0.35]`
  - Red Mountain Crater Lake: `[0.2, 0.15, 0.1]` (reddish volcanic)
  - Seyda Neen Harbor: `[0.12, 0.22, 0.32]`
  - Balmora River: `[0.08, 0.2, 0.25]`
  - Pelagiad Lake: `[0.1, 0.25, 0.35]`

---

## ⏳ REMAINING TASKS

### Task 5: Update WaterManager Method Signatures
**Priority**: HIGH
**Files**: `apps/openmw/mwrender/water.hpp` (line 182-183), `apps/openmw/mwrender/water.cpp` (line 1178-1191)

#### What Needs to Change:

**In water.hpp:**
```cpp
// CURRENT:
void addLakeCell(int gridX, int gridY, float height);
void addLakeAtWorldPos(float worldX, float worldY, float height);

// CHANGE TO:
void addLakeCell(int gridX, int gridY, float height,
                 const osg::Vec3f& waterColor = osg::Vec3f(0.15f, 0.25f, 0.35f));
void addLakeAtWorldPos(float worldX, float worldY, float height,
                       const osg::Vec3f& waterColor = osg::Vec3f(0.15f, 0.25f, 0.35f));
```

**In water.cpp:**
```cpp
void WaterManager::addLakeCell(int gridX, int gridY, float height, const osg::Vec3f& waterColor)
{
    if (mLake)
        mLake->addWaterCell(gridX, gridY, height, waterColor);
}

void WaterManager::addLakeAtWorldPos(float worldX, float worldY, float height, const osg::Vec3f& waterColor)
{
    int gridX = static_cast<int>(std::floor(worldX / 8192.0f));
    int gridY = static_cast<int>(std::floor(worldY / 8192.0f));
    addLakeCell(gridX, gridY, height, waterColor);
}
```

#### Why This Task is Needed:
WaterManager is the public API that other systems use to add lake cells. It must forward the water color parameter to the Lake class.

---

### Task 6: Pass Water Color from Cell Loading to Lake System
**Priority**: HIGH
**Estimated Difficulty**: Medium

#### Investigation Needed:
1. **Find where cells with water trigger lake creation**
   - Search codebase for calls to `addLakeCell()` or `addLakeAtWorldPos()`
   - Likely in cell loading code: `apps/openmw/mwworld/` or `apps/openmw/mwrender/`

2. **Verify cell data is available at those locations**
   - Check if `MWWorld::Cell` or `CellStore` is accessible
   - Verify we can call `cell.hasWaterColor()` and getters

#### Implementation Pattern:
```cpp
// Somewhere in cell loading code where addLakeCell is called:

const MWWorld::Cell& cellData = store->getCell();

if (cellData.hasWaterColor()) {
    // Use custom water color from WCOL subrecord
    osg::Vec3f waterColor(
        cellData.getWaterColorR(),
        cellData.getWaterColorG(),
        cellData.getWaterColorB()
    );
    waterManager->addLakeCell(gridX, gridY, waterHeight, waterColor);
} else {
    // Use default water color
    waterManager->addLakeCell(gridX, gridY, waterHeight);
}
```

#### Files to Search:
```bash
# Find existing calls to addLakeCell:
grep -rn "addLakeCell" apps/openmw/

# Find where cells are loaded/changed:
grep -rn "changeCell\|loadCell" apps/openmw/mwworld/
grep -rn "CellStore" apps/openmw/mwrender/water.cpp
```

#### Expected Locations:
- `apps/openmw/mwrender/water.cpp` - `changeCell()` or `addCell()` methods
- `apps/openmw/mwworld/scene.cpp` - Cell activation/loading
- Possibly in `loadLakesFromJSON()` if JSON support is kept

---

### Task 7: Verify Lake Shader Uses waterColor Uniform
**Priority**: MEDIUM
**Estimated Difficulty**: Low

#### What to Check:
1. **Find the lake fragment shader**:
   ```bash
   find . -name "lake.frag" -o -name "*lake*.glsl"
   ```
   Likely: `files/shaders/compatibility/lake.frag`

2. **Verify the shader declares the uniform**:
   ```glsl
   uniform vec3 waterColor;
   ```

3. **Verify the shader applies the color**:
   ```glsl
   // Should multiply final color by waterColor
   vec3 finalColor = /* reflection/SSR calculations */;
   finalColor *= waterColor;  // Apply per-cell tint
   gl_FragColor = vec4(finalColor, alpha);
   ```

#### If Uniform is Missing:
The uniform is already being set in `lake.cpp:511`, so the shader MUST declare and use it, otherwise it will be silently ignored.

**Action Required**: Add the uniform declaration and color multiplication to the shader.

---

### Task 8: Create Technical Documentation
**Priority**: LOW
**File to Create**: `docs/WCOL_Subrecord_Specification.md`

#### Documentation Sections:

1. **WCOL Subrecord Format**
   - Size: 12 bytes
   - Structure: 3 × float32 (R, G, B)
   - Value range: 0.0 - 1.0 per component
   - Parent record: CELL

2. **Creating .omwaddon Files with WCOL**
   - Tools: TES3CMD, OpenMW Construction Set
   - Step-by-step guide for adding WCOL to cells
   - Example values for different water types

3. **Load Order Behavior**
   - Last plugin wins (standard ESM override)
   - How multiple plugins interact

4. **Validation Rules**
   - What happens with invalid values (NaN, Inf, out of range)
   - Warning messages and fallback behavior

5. **Examples**
   ```
   Volcanic lake (reddish):  [0.3, 0.15, 0.1]
   Clear mountain lake:      [0.1, 0.2, 0.4]
   Swamp water (greenish):   [0.15, 0.25, 0.15]
   Default (no WCOL):        [0.15, 0.25, 0.35]
   ```

---

## Testing Checklist

### Unit Testing
- [ ] Load a cell with WCOL subrecord
- [ ] Verify water color values are read correctly
- [ ] Test invalid values trigger warnings
- [ ] Test cells without WCOL use defaults

### Integration Testing
- [ ] Create test .omwaddon with WCOL subrecords
- [ ] Load in OpenMW and verify color changes
- [ ] Test multiple plugins overriding same cell
- [ ] Test both interior and exterior cells

### Visual Verification
- [ ] Water appears with correct color tint
- [ ] Color transitions smoothly between cells
- [ ] Reflections/SSR work with colored water
- [ ] No performance regression

---

## Known Limitations

1. **JSON vs .omwaddon**:
   - JSON approach (`lakes.json`) is temporary
   - .omwaddon is the proper solution
   - `loadLakesFromJSON()` may need updating or removal

2. **Per-Cell vs Per-Lake**:
   - Currently per-cell (matches ESM structure)
   - Multi-cell lakes require same WCOL in all cells
   - Future enhancement: lake regions spanning multiple cells

3. **Shader Compatibility**:
   - Requires shader with `waterColor` uniform support
   - Older/fallback shaders may ignore color

---

## File Change Summary

| File | Status | Lines Changed | Purpose |
|------|--------|---------------|---------|
| `components/esm3/loadcell.hpp` | ✅ Complete | +7 | Added water color fields to Cell struct |
| `components/esm3/loadcell.cpp` | ✅ Complete | +30 | WCOL parse/save/validation |
| `apps/openmw/mwworld/cell.hpp` | ✅ Complete | +9 | Public getters + private members |
| `apps/openmw/mwworld/cell.cpp` | ✅ Complete | +4 | Constructor initialization |
| `apps/openmw/mwrender/lake.hpp` | ✅ Complete | +3 | waterColor in CellWater + getter |
| `apps/openmw/mwrender/lake.cpp` | ✅ Complete | +16 | Color param + uniform setup |
| `MultiLevelWater/lakes.json` | ✅ Complete | Full rewrite | Sample lakes with colors |
| `apps/openmw/mwrender/water.hpp` | ⏳ **TODO** | ~2 | Add waterColor parameters |
| `apps/openmw/mwrender/water.cpp` | ⏳ **TODO** | ~8 | Forward waterColor to Lake |
| Cell loading code | ⏳ **TODO** | ~15 | Pass color from cells |
| Lake shader | ⏳ **TODO** | Verification | Ensure uniform is used |
| `docs/WCOL_Subrecord_Specification.md` | ⏳ **TODO** | New file | Technical documentation |

---

## Next Steps for Continuation

1. **Edit `water.hpp` and `water.cpp`** (Task 5)
   - Add waterColor parameter with default value
   - Update both method signatures
   - Forward parameter to `mLake->addWaterCell()`

2. **Find cell loading integration point** (Task 6)
   - Search for existing `addLakeCell()` calls
   - Add water color extraction from `MWWorld::Cell`
   - Test with cells that have WCOL subrecords

3. **Verify shader** (Task 7)
   - Locate lake fragment shader
   - Confirm `waterColor` uniform usage
   - Add if missing

4. **Write documentation** (Task 8)
   - Create specification document
   - Include examples and tool usage
   - Document for mod creators

5. **Create test .omwaddon**
   - Use TES3CMD or OpenMW-CS
   - Add WCOL to a test cell
   - Verify in-game

---

## Design Rationale

### Why .omwaddon Instead of JSON?
- **Native mod system**: Uses OpenMW's plugin load order
- **Tool support**: Compatible with OpenMW Construction Set
- **Standard format**: ESM3 format is well-documented
- **Modder-friendly**: Standard Morrowind modding workflow

### Why Per-Cell Instead of Per-Lake?
- **ESM structure**: Cells are the natural unit in ESM format
- **Fine control**: Modders can override specific cells
- **Simplicity**: No need for complex lake boundary definitions

### Default Color Choice: (0.15, 0.25, 0.35)
- Matches original Morrowind water appearance
- Slightly blue-green tint
- Safe fallback when WCOL not specified

---

## Contact & History

**Implementation Date**: 2025-11-27
**OpenMW Version Target**: Current development branch
**Feature Branch**: waterlevels

**Implementers**: Claude (AI Assistant) with user guidance

---

*This document will be updated as remaining tasks are completed.*
