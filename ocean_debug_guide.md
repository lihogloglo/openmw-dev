# Ocean Debug Visualization Guide

## Overview

The ocean implementation now includes a cascade visualization debug mode to help understand how the different wave cascades cover the ocean surface.

## Changes Made

### 1. Displacement Strength
- **Reduced from 100x to 10x** for more realistic wave heights
- Location: [ocean.vert:51](d:\Gamedev\openmw-snow\files\shaders\compatibility\ocean.vert#L51)

### 2. Cascade Visualization
- **New debug mode** to visualize cascade coverage with color coding
- **Color scheme**:
  - 🔴 **Red**: Cascade 0 (50m tiles - finest detail, small ripples)
  - 🟢 **Green**: Cascade 1 (100m tiles - medium waves)
  - 🔵 **Blue**: Cascade 2 (200m tiles - larger swells)
  - 🟡 **Yellow**: Cascade 3 (400m tiles - broadest ocean waves)
- **Grid lines** show tile boundaries for each cascade

## How to Enable Debug Mode

### Method 1: Via Code (Temporary for Testing)

Add this to wherever you have access to the Ocean instance:

```cpp
// Enable cascade visualization
ocean->setDebugVisualizeCascades(true);

// Disable it
ocean->setDebugVisualizeCascades(false);
```

### Method 2: Console Command (Recommended)

You'll need to add a console command to toggle this. Here's where to add it:

1. Find the console command handler (typically in `apps/openmw/mwbase/` or similar)
2. Add a command like:
   ```cpp
   registerCommand("toggleOceanCascades", "Toggle ocean cascade debug visualization",
       [](const std::vector<std::string>&) {
           // Get ocean instance from render manager
           MWRender::Ocean* ocean = /* get ocean from rendering manager */;
           if (ocean) {
               static bool enabled = false;
               enabled = !enabled;
               ocean->setDebugVisualizeCascades(enabled);
               return enabled ? "Cascade debug ON" : "Cascade debug OFF";
           }
       });
   ```

### Method 3: Keyboard Shortcut (Alternative)

Add a keybinding in your input handler:

```cpp
if (key == KEY_F8 && ocean) {
    static bool debugCascades = false;
    debugCascades = !debugCascades;
    ocean->setDebugVisualizeCascades(debugCascades);
}
```

## What You'll See in Debug Mode

When enabled, the ocean will display colored regions showing:

1. **Cascade Coverage**: Each cascade is color-coded (red/green/blue/yellow)
2. **Tile Boundaries**: Black grid lines show where each cascade's tiles repeat
3. **Cascade Overlap**: All cascades are rendered additively, so you see their combined contribution

### Understanding the Visualization

- **Near the camera**: You'll see more red (finest detail cascade)
- **Medium distance**: Green and blue appear
- **Far away**: Yellow dominates (largest waves)
- **Grid pattern**: Shows the repeating tile structure of each cascade

The repeating grid pattern is normal - each cascade uses a periodic FFT texture that tiles across the ocean surface.

## Technical Details

### Cascade Parameters (in Morrowind Units)

| Cascade | Size (meters) | Size (MW units) | UV Scale | Purpose |
|---------|---------------|-----------------|----------|---------|
| 0 | 50m | 3,626.5 | 0.000276 | Fine ripples, chop |
| 1 | 100m | 7,253.0 | 0.000138 | Medium waves |
| 2 | 200m | 14,506.0 | 0.000069 | Large swells |
| 3 | 400m | 29,012.0 | 0.000034 | Broad ocean waves |

### Files Modified

1. **ocean.vert** - Reduced displacement multiplier
2. **ocean.frag** - Added cascade visualization shader code
3. **ocean.hpp** - Added `setDebugVisualizeCascades()` method
4. **ocean.cpp** - Added debug uniform and setter implementation

## Tips for Tuning

While in debug mode, you can:

1. **Verify cascade coverage** - Ensure each cascade is visible and contributing
2. **Check tile boundaries** - Grid lines shouldn't be too visible in normal rendering
3. **Adjust cascade sizes** - If colors blend poorly, tweak tile sizes in ocean.cpp
4. **Test LOD transitions** - Move around to see how cascades blend at different distances

## Returning to Normal Rendering

Simply call:
```cpp
ocean->setDebugVisualizeCascades(false);
```

The ocean will return to normal water rendering with proper lighting, normals, and transparency.
