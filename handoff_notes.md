# Ocean Rendering Debug Session - Handoff Notes

## Problem Summary
The ocean renders as a flat purple surface (Normals = 0,0,1) due to SSBO data transfer failure.

## Investigation Timeline

### What Works
1. ✅ **Geometry is visible** - Ocean mesh renders correctly
2. ✅ **Initial Spectrum Generation** - Saw static noise pattern
3. ✅ **Physics Math** - Saw green/blue pattern in spectrum_modulate

### What's Broken
❌ **SSBO Data Transfer** - Data written by `spectrum_modulate` is not reaching `fft_unpack`

## Debug Attempts
1. **Bypassed FFT** - Disabled FFT stages to isolate the issue
2. **Added Debug Patterns** - Modified `spectrum_modulate` to write simple test patterns:
   - Layer 0: `vec2(1.0, 0.0)` (Red)
   - Layer 1: `vec2(0.0, 1.0)` (Green)  
   - Layer 2: `vec2(0.5, 0.5)` (Gray)
   - Layer 3: `vec2(1.0, 1.0)` (Yellow)
3. **Result**: Solid Purple (zeros) instead of expected patterns
4. **Fixed SSBO Binding Mismatch** (2025-11-22)
   - Changed `fft_unpack.comp` binding from 0 to 1.
   - Updated `ocean.cpp` to bind to 1.
   - **Result**: Still Purple (implies shader not writing or not running).
5. **Force Green Output** (Current Step)
   - Modified `fft_unpack.comp` to write `vec4(0, 100, 0, 1)` (Green) to `normal_map`.
   - Modified `ocean.frag` to visualize `normal_map` directly.

## Next Steps
1. **Check Ocean Color**:
   - **Green**: `fft_unpack` is running and writing! The issue was the SSBO reading (zeros). Revert debug changes and debug SSBO data.
   - **Blue (0,0,1)**: `fft_unpack` is NOT writing. `normal_map` has initial values. Investigate `dispatchCompute` or `imageStore`.
   - **Black**: `normal_map` is invalid/empty.
   - **Purple**: `ocean.frag` logic is still dominating or `normalMap` has weird values.

## Root Cause Found (2025-11-22)
**SSBO Binding Mismatch!** (Fixed, but verification pending)

### The Issue
The FFT buffer is bound to different binding points in different shaders:

**In spectrum_modulate.comp (line 19):**
```glsl
layout(std430, binding = 1) restrict writeonly buffer FFTBuffer
```

**In fft_unpack.comp (line 15):**
```glsl
layout(std430, binding = 0) restrict buffer FFTBuffer
```

### C++ Bindings
**In ocean.cpp updateComputeShaders():**
- Line 551: Modulate writes to binding 1: `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftBufferID);`
- Line 651: Unpack reads from binding 0: `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, fftBufferID);`

The C++ code correctly binds to what each shader declares, but the shaders themselves disagree on which binding to use!

## Solution
Standardize SSBO bindings across all shaders. Recommended binding layout:
- Binding 0: Butterfly factors buffer (used by fft_butterfly and fft_compute)
- Binding 1: FFT working buffer (used by modulate, fft_compute, transpose, unpack)

**Required Changes:**
1. Change `fft_unpack.comp` binding from 0 to 1 (DONE)
2. Ensure all FFT pipeline shaders use consistent bindings (DONE)
3. Update `ocean.cpp` to use binding 1 for unpack (DONE)

## Current Code State
- FFT stages are disabled (commented out in ocean.cpp lines 580-626)
- Debug patterns active in spectrum_modulate.comp (lines 83-87)
- **FORCE GREEN** active in fft_unpack.comp
- **DIRECT VISUALIZATION** active in ocean.frag

