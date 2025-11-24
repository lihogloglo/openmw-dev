# Ocean Console Commands

The FFT ocean system now supports runtime configuration of all major parameters through the in-game console.

## Accessing the Ocean Parameters

All ocean parameters can be accessed through the Lua console. Open the console with the tilde key (`~`) and use the following Lua code:

## Available Parameters

### Colors

**Water Color** - RGB color of the ocean water (default: 0.15, 0.25, 0.35)
```lua
-- Get current water color
local water = openmw.core.getRenderingManager():getWater()
local color = water:getOceanWaterColor()

-- Set water color (R, G, B values from 0.0 to 1.0)
water:setOceanWaterColor(0.15, 0.25, 0.35)  -- Default blue-cyan
water:setOceanWaterColor(0.1, 0.3, 0.2)     -- More green
water:setOceanWaterColor(0.2, 0.15, 0.25)   -- Purple-ish
```

**Foam Color** - RGB color of wave foam/whitecaps (default: 1.0, 1.0, 1.0)
```lua
-- Get current foam color
local foam = water:getOceanFoamColor()

-- Set foam color
water:setOceanFoamColor(1.0, 1.0, 1.0)      -- White (default)
water:setOceanFoamColor(0.95, 0.9, 0.85)    -- Slightly cream-colored
```

### Wave Physics Parameters

**Wind Speed** - Speed of wind in meters per second (default: 20.0 m/s)
```lua
-- Get current wind speed
local speed = water:getOceanWindSpeed()

-- Set wind speed (affects wave height and steepness)
water:setOceanWindSpeed(5.0)    -- Calm (5 m/s, ~11 mph)
water:setOceanWindSpeed(10.0)   -- Light breeze (10 m/s, ~22 mph)
water:setOceanWindSpeed(20.0)   -- Moderate wind (20 m/s, ~45 mph) [DEFAULT]
water:setOceanWindSpeed(30.0)   -- Strong wind (30 m/s, ~67 mph)
water:setOceanWindSpeed(40.0)   -- Storm (40 m/s, ~89 mph)
```

**Wind Direction** - Direction of wind in degrees (default: 0.0 = North)
```lua
-- Get current wind direction
local dir = water:getOceanWindDirection()

-- Set wind direction (0 = North, 90 = East, 180 = South, 270 = West)
water:setOceanWindDirection(0.0)     -- From the north
water:setOceanWindDirection(90.0)    -- From the east
water:setOceanWindDirection(180.0)   -- From the south
water:setOceanWindDirection(270.0)   -- From the west
```

**Fetch Length** - Distance wind has blown over water in meters (default: 550000.0 = 550 km)
```lua
-- Get current fetch length
local fetch = water:getOceanFetchLength()

-- Set fetch length (affects wave period and steepness)
water:setOceanFetchLength(50000.0)    -- 50 km - shorter waves
water:setOceanFetchLength(150000.0)   -- 150 km - medium waves
water:setOceanFetchLength(550000.0)   -- 550 km - long waves [DEFAULT]
water:setOceanFetchLength(1000000.0)  -- 1000 km - very long period waves
```

**Swell** - Wave elongation factor (default: 0.8, range: 0.0-2.0)
```lua
-- Get current swell
local swell = water:getOceanSwell()

-- Set swell (higher = more elongated waves, lower = more circular)
water:setOceanSwell(0.0)   -- Circular waves
water:setOceanSwell(0.5)   -- Slightly elongated
water:setOceanSwell(0.8)   -- Elongated [DEFAULT]
water:setOceanSwell(1.5)   -- Very elongated
```

**Detail** - High frequency wave attenuation (default: 1.0, range: 0.0-1.0)
```lua
-- Get current detail
local detail = water:getOceanDetail()

-- Set detail (higher = more small ripples, lower = smoother)
water:setOceanDetail(0.0)   -- Smooth, no small waves
water:setOceanDetail(0.5)   -- Moderate detail
water:setOceanDetail(1.0)   -- Full detail [DEFAULT]
```

**Spread** - Directional wave spreading (default: 0.2, range: 0.0-1.0)
```lua
-- Get current spread
local spread = water:getOceanSpread()

-- Set spread (higher = more wave directions, lower = more aligned with wind)
water:setOceanSpread(0.0)   -- All waves aligned with wind
water:setOceanSpread(0.2)   -- Slight spreading [DEFAULT]
water:setOceanSpread(0.5)   -- Moderate spreading
water:setOceanSpread(1.0)   -- Maximum spreading
```

**Foam Amount** - Whitecap foam intensity (default: 5.0, range: 0.0-10.0)
```lua
-- Get current foam amount
local foam = water:getOceanFoamAmount()

-- Set foam amount (affects foam accumulation and decay rates)
water:setOceanFoamAmount(0.0)   -- No foam
water:setOceanFoamAmount(2.0)   -- Minimal foam
water:setOceanFoamAmount(5.0)   -- Moderate foam [DEFAULT]
water:setOceanFoamAmount(8.0)   -- Heavy foam
water:setOceanFoamAmount(10.0)  -- Maximum foam
```

## Example Scripts

### Calm Ocean
```lua
local water = openmw.core.getRenderingManager():getWater()
water:setOceanWindSpeed(5.0)
water:setOceanFetchLength(50000.0)
water:setOceanSwell(0.5)
water:setOceanFoamAmount(1.0)
```

### Stormy Ocean
```lua
local water = openmw.core.getRenderingManager():getWater()
water:setOceanWindSpeed(35.0)
water:setOceanFetchLength(800000.0)
water:setOceanSwell(1.2)
water:setOceanFoamAmount(9.0)
```

### Tropical Waters (Bright Turquoise)
```lua
local water = openmw.core.getRenderingManager():getWater()
water:setOceanWaterColor(0.1, 0.4, 0.45)
water:setOceanWindSpeed(12.0)
```

### Arctic Waters (Dark Blue-Gray)
```lua
local water = openmw.core.getRenderingManager():getWater()
water:setOceanWaterColor(0.12, 0.18, 0.25)
water:setOceanWindSpeed(25.0)
water:setOceanFoamAmount(7.0)
```

## Important Notes

1. **Unit Conversion**: Remember that Morrowind uses its own unit system where 22.1 units = 1 foot. The ocean parameters use real-world units (meters, meters/second) which are automatically converted internally.

2. **Spectrum Regeneration**: Changing wave physics parameters (wind speed, direction, fetch, swell, detail, spread, foam amount) will trigger spectrum regeneration on the next frame. This is automatic but may cause a brief stutter.

3. **Color Changes**: Water and foam color changes take effect immediately without regenerating the wave spectrum.

4. **Parameter Ranges**: While the system won't crash with extreme values, staying within the suggested ranges will give the most realistic results.

5. **Accessing from Console**: The Lua bindings may not be available from the in-game console by default. You may need to create a Lua script and load it as a mod for runtime access. Alternatively, these parameters could be exposed through C++ console commands (requires additional implementation).

## Future Enhancements

Potential additions for easier access:
- Direct console commands (e.g., `setOceanWindSpeed 25`)
- Settings file support for persistent configuration
- Preset ocean conditions (calm/moderate/stormy)
- Time-of-day based automatic adjustments
- Weather system integration
