# Prompt for Next Agent: Ocean Console Commands

## Context

I'm implementing an FFT ocean in OpenMW (similar to GodotOceanWaves). The C++ infrastructure for runtime-configurable ocean parameters is **100% complete and working**, but I need help making these parameters accessible from the in-game console.

## What's Already Working ✅

The following C++ methods work perfectly:

```cpp
MWRender::WaterManager* water = getRenderingManager()->getWater();

// All of these work:
water->setOceanWindSpeed(30.0f);
water->setOceanWaterColor(osg::Vec3f(0.1f, 0.4f, 0.45f));
water->setOceanFoamColor(osg::Vec3f(1.0f, 1.0f, 1.0f));
water->setOceanWindDirection(180.0f);
water->setOceanFetchLength(800000.0f);
water->setOceanSwell(1.2f);
water->setOceanDetail(0.8f);
water->setOceanSpread(0.3f);
water->setOceanFoamAmount(8.0f);

float speed = water->getOceanWindSpeed();  // Also works
```

## The Problem ❌

I tried creating MWScript console commands but they don't work:
- Created `oceanextensions.cpp/hpp` with opcodes
- Registered in `opcodes.hpp`, `extensions0.cpp`, `CMakeLists.txt`
- Builds successfully
- But typing `setoceanwindspeed 5.0` in console gives "Unexpected name" error

**Files I created** (that don't work):
- `apps/openmw/mwscript/oceanextensions.hpp`
- `apps/openmw/mwscript/oceanextensions.cpp`
- Modified: `components/compiler/opcodes.hpp`, `extensions0.hpp`, `extensions0.cpp`, etc.

## What I Need Help With

**Goal**: Make these 9 parameters editable from the in-game console (press `~` key):

1. Water color (RGB)
2. Foam color (RGB)
3. Wind speed (float)
4. Wind direction (degrees)
5. Fetch length (meters)
6. Swell (0-2)
7. Detail (0-1)
8. Spread (0-1)
9. Foam amount (0-10)

## Three Possible Approaches

### Option 1: Fix MWScript Console Commands (what I tried)
- Research how existing console-only commands work (e.g., `togglecollision`, `togglefogofwar`)
- Find what's missing from my implementation
- Get commands like `setoceanwindspeed 30.0` working

### Option 2: Create Lua Bindings (probably easier)
- Look at `apps/openmw/mwlua/camerabindings.cpp` as example
- Create `oceanbindings.cpp` to expose WaterManager methods
- Register in `luabindings.cpp`
- Use from Lua console: `openmw.ocean.setWindSpeed(30.0)`

### Option 3: Settings UI (best UX, more work)
- Add sliders to OpenMW settings menu
- Or create ImGui debug panel
- Most user-friendly but requires UI work

## My Recommendation

**Try Option 2 (Lua bindings) first.** Here's why:
- OpenMW has mature Lua bindings infrastructure
- Easier to debug than MWScript opcodes
- More maintainable long-term
- Can be used by mods too

## Key Files to Check

**For Lua approach**:
- `apps/openmw/mwlua/camerabindings.cpp` - Good example
- `apps/openmw/mwlua/luabindings.cpp` - Where to register
- `apps/openmw/mwbase/world.hpp` - How to access World
- My `apps/openmw/mwrender/water.hpp` - Methods to expose

**For MWScript approach**:
- `apps/openmw/mwscript/miscextensions.cpp` - Working example
- `apps/openmw/mwscript/consoleextensions.hpp` - Console-only commands
- My `apps/openmw/mwscript/oceanextensions.cpp` - What I tried

## What Success Looks Like

User opens console with `~` and can type:
```
setoceanwindspeed 30.0         # Storm
setoceanwatercolor 0.1 0.4 0.45  # Tropical blue
getoceanwindspeed              # Returns: 30.0
```

Or with Lua:
```lua
openmw.ocean.setWindSpeed(30.0)
openmw.ocean.setWaterColor(0.1, 0.4, 0.45)
local speed = openmw.ocean.getWindSpeed()
```

## Documentation

Check these files in the repo:
- `OCEAN_RUNTIME_PARAMS_STATUS.md` - Full status of what's done
- `OCEAN_IMPLEMENTATION_TRACKING.md` - Overall project tracking
- `OCEAN_CONSOLE_COMMANDS_SIMPLE.md` - User guide (when commands work)

## Important Note

**The hard work is done!** All the C++ infrastructure works perfectly. This is purely an "exposure layer" problem - we just need to connect the working C++ methods to the console. The actual ocean parameter system is solid and tested.

## What to Do

1. Read `OCEAN_RUNTIME_PARAMS_STATUS.md` to understand what's complete
2. Choose an approach (I recommend Lua bindings)
3. Study existing examples in OpenMW codebase
4. Implement and test
5. Update documentation when working

Good luck! The finish line is close.
