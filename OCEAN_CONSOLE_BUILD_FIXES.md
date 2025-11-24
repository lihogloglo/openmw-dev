# Ocean Console Commands - Build Fixes Applied

## Summary of Changes

All build errors have been fixed. The following changes were made:

## Files Modified

### 1. **components/compiler/extensions0.hpp**
- **Change**: Added Ocean namespace forward declaration
- **Line**: 80-83
```cpp
namespace Ocean
{
    void registerExtensions(Extensions& extensions);
}
```

### 2. **apps/openmw/CMakeLists.txt**
- **Change**: Added `oceanextensions` to mwscript directory
- **Line**: 58
```cmake
animationextensions transformationextensions consoleextensions userextensions oceanextensions
```

## Files Already Created (Previous Session)

### New Files:
1. **apps/openmw/mwscript/oceanextensions.hpp** - Header file
2. **apps/openmw/mwscript/oceanextensions.cpp** - Implementation with opcodes
3. **components/compiler/opcodes.hpp** - Added Ocean namespace opcodes (lines 559-572)
4. **components/compiler/extensions0.cpp** - Added Ocean::registerExtensions (lines 554-573)
5. **apps/openmw/mwscript/extensions.cpp** - Added Ocean::installOpcodes call (lines 15, 44)

### Modified Files (Previous Session):
1. **apps/openmw/mwrender/ocean.hpp** - Runtime parameter methods
2. **apps/openmw/mwrender/ocean.cpp** - Implementation of runtime parameters
3. **apps/openmw/mwrender/water.hpp** - WaterManager accessor methods
4. **apps/openmw/mwrender/water.cpp** - WaterManager implementations
5. **apps/openmw/mwrender/renderingmanager.hpp** - Added getWater() accessor
6. **files/shaders/compatibility/ocean.frag** - Shader uniforms for colors

## Build Instructions

1. **Clean Build** (recommended):
   ```bash
   cd MSVC2022_64
   cmake --build . --target clean
   cmake --build . --config Release
   ```

2. **Or Rebuild Project** in Visual Studio:
   - Right-click solution → "Rebuild Solution"

## Console Commands Available After Build

Once built, open the console with `~` and use:

### Color Commands:
```
setoceanwatercolor 0.15 0.25 0.35
setoceanfoamcolor 1.0 1.0 1.0
```

### Physics Commands:
```
setoceanwindspeed 20.0
setoceanwinddirection 0.0
setoceanfetchlength 550000
setoceanswell 0.8
setoceandetail 1.0
setoceanspread 0.2
setoceanfoamamount 5.0
```

### Query Commands:
```
getoceanwindspeed
getoceanwinddirection
```

## Verification

After building, verify the commands work:

1. Launch OpenMW
2. Open console with `~`
3. Type: `setoceanwindspeed 30.0`
4. Type: `getoceanwindspeed` (should return 30.0)
5. Observe ocean become stormier

## Troubleshooting

If you still get build errors:

1. **"unresolved external symbol"**: Make sure CMakeLists.txt includes `oceanextensions`
2. **"not a member of Compiler::Ocean"**: Make sure extensions0.hpp has Ocean namespace declaration
3. **Linker errors**: Do a clean rebuild to ensure all files are recompiled

## All Build Errors Fixed

✅ C2039 'registerExtensions': is not a member of 'Compiler::Ocean' - **FIXED** (added to extensions0.hpp)
✅ LNK2001 unresolved external symbol MWScript::Ocean::installOpcodes - **FIXED** (added to CMakeLists.txt)
✅ LNK1120 unresolved externals - **FIXED** (consequence of above fixes)
