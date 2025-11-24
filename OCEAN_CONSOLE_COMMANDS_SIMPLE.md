# Ocean Console Commands - Quick Guide

## How to Use

Open the in-game console with the tilde key (`~`) and type these commands directly:

## Commands

### Water and Foam Colors

```
setoceanwatercolor <R> <G> <B>
setoceanfoamcolor <R> <G> <B>
```

RGB values are 0.0 to 1.0

**Examples:**
```
setoceanwatercolor 0.15 0.25 0.35    # Default ocean blue
setoceanwatercolor 0.1 0.4 0.45      # Tropical turquoise
setoceanwatercolor 0.12 0.18 0.25    # Arctic dark blue
setoceanfoamcolor 1.0 1.0 1.0        # White foam
setoceanfoamcolor 0.95 0.9 0.85      # Cream foam
```

### Wave Physics

```
setoceanwindspeed <speed>           # m/s (default: 20.0)
setoceanwinddirection <degrees>     # 0=North, 90=East, 180=South, 270=West
setoceanfetchlength <meters>        # distance wind has blown (default: 550000)
setoceanswell <value>               # 0.0-2.0 (default: 0.8)
setoceandetail <value>              # 0.0-1.0 (default: 1.0)
setoceanspread <value>              # 0.0-1.0 (default: 0.2)
setoceanfoamamount <value>          # 0.0-10.0 (default: 5.0)
```

**Examples:**
```
# Calm ocean
setoceanwindspeed 5.0
setoceanfetchlength 50000
setoceanswell 0.5
setoceanfoamamount 1.0

# Storm
setoceanwindspeed 35.0
setoceanfetchlength 800000
setoceanswell 1.2
setoceanfoamamount 9.0

# Light breeze
setoceanwindspeed 10.0
setoceanwinddirection 90
setoceanfetchlength 150000

# Perfect waves (surfing conditions)
setoceanwindspeed 15.0
setoceanfetchlength 300000
setoceanswell 1.0
setoceandetail 0.8
```

### Query Current Values

```
getoceanwindspeed
getoceanwinddirection
```

These return the current values.

## Quick Presets

### Calm Bay
```
setoceanwindspeed 5.0
setoceanfetchlength 50000
setoceanswell 0.5
setoceandetail 0.7
setoceanspread 0.3
setoceanfoamamount 1.0
```

### Normal Ocean
```
setoceanwindspeed 20.0
setoceanfetchlength 550000
setoceanswell 0.8
setoceandetail 1.0
setoceanspread 0.2
setoceanfoamamount 5.0
```

### Stormy Seas
```
setoceanwindspeed 35.0
setoceanfetchlength 800000
setoceanswell 1.2
setoceandetail 1.0
setoceanspread 0.3
setoceanfoamamount 9.0
setoceanwatercolor 0.1 0.15 0.2
```

### Tropical Paradise
```
setoceanwatercolor 0.1 0.4 0.45
setoceanwindspeed 12.0
setoceanfetchlength 200000
setoceanswell 0.6
setoceanfoamamount 3.0
```

### Arctic Waters
```
setoceanwatercolor 0.12 0.18 0.25
setoceanwindspeed 25.0
setoceanfetchlength 600000
setoceanswell 0.9
setoceanfoamamount 7.0
```

## Tips

1. **Color Changes**: Water and foam color changes are instant and have no performance impact.

2. **Wave Physics Changes**: Changing wind speed, direction, fetch, swell, detail, spread, or foam amount will regenerate the wave spectrum. This may cause a brief stutter the first time, but subsequent changes are smoother.

3. **Wind Speed Guide**:
   - 0-5 m/s: Calm (~0-11 mph)
   - 5-15 m/s: Light breeze (~11-33 mph)
   - 15-25 m/s: Moderate wind (~33-56 mph)
   - 25-35 m/s: Strong wind (~56-78 mph)
   - 35+ m/s: Storm conditions (~78+ mph)

4. **Fetch Length**: Longer fetch = longer period waves (swells). Shorter fetch = choppier, wind-driven waves.

5. **Swell**: Higher values make waves more elongated in the wind direction. Lower values create more circular wave patterns.

6. **Detail**: Controls high-frequency small waves. 1.0 = maximum ripples, 0.0 = smooth surface.

7. **Spread**: Controls how directional the waves are. 0.0 = all waves in wind direction, 1.0 = waves from all directions.

8. **Foam Amount**: Controls how much white foam/whitecaps appear on breaking waves.

## Note

All console commands are case-insensitive. You can type `SetOceanWindSpeed`, `setoceanwindspeed`, or `SETOCEANWINDSPEED` - they all work the same.
