# GodotOceanWaves Shader Pipeline Analysis

## Overview
The GodotOceanWaves implementation uses a **Stockham FFT** algorithm with **6 compute shaders** to create realistic ocean waves using JONSWAP/TMA spectrum.

## Shader Pipeline

### 1. **spectrum_compute.glsl** - Initial Spectrum Generation
- **Purpose**: Generates JONSWAP/TMA wave spectrum with Hasselmann directional spreading
- **Runs**: Once at initialization or when parameters change
- **Input**: Push constants (wind speed, direction, depth, etc.)
- **Output**: `spectrum` (image2DArray rgba16f) - Initial wave spectrum H0(k) and H0(-k)
- **Key Features**:
  - TMA spectrum (better shallow water model than Phillips)
  - Directional spreading for realistic wave patterns
  - Depth attenuation
  - Box-Muller transform for Gaussian random numbers

### 2. **spectrum_modulate.glsl** - Time Evolution
- **Purpose**: Modulates spectrum over time and calculates gradients
- **Runs**: Every frame
- **Input**:
  - `spectrum` (read-only)
  - Time, depth, tile length (push constants)
- **Output**: `FFTBuffer.data[]` - Time-evolved spectrum with displacement and gradients packed
- **Packs 4 spectra**:
  1. hx (horizontal X) and hy (vertical height)
  2. hz (horizontal Z) and dhy/dx (height gradient X)
  3. dhy/dz (height gradient Z) and dhx/dx
  4. dhz/dz and dhz/dx
- **Uses dispersion relation**: ω = sqrt(g*k*tanh(k*depth))

### 3. **fft_butterfly.glsl** - Precompute Butterfly Factors
- **Purpose**: Precomputes twiddle factors for Stockham FFT
- **Runs**: Once at initialization
- **Output**: `ButterflyFactorBuffer.butterfly[]` - Twiddle factors and read indices
- **Size**: log2(map_size) × map_size

### 4. **fft_compute.glsl** - Row-wise FFT
- **Purpose**: Performs Stockham FFT on rows (1D FFT along X axis)
- **Runs**: Once per cascade per frame
- **Input**: `FFTBuffer.data[]` (input layer)
- **Output**: `FFTBuffer.data[]` (output layer - ping-pong buffering)
- **Uses**: Shared memory for coalescing
- **Processes**: All 4 spectra in parallel

### 5. **transpose.glsl** - Matrix Transpose
- **Purpose**: Transposes the FFT result to prepare for column-wise FFT
- **Runs**: Once per cascade per frame
- **Uses**: Shared memory tiling (32×32) for efficient transpose
- **Ping-pong**: Swaps input/output layers

### 6. **fft_compute.glsl** - Column-wise FFT
- **Purpose**: Performs Stockham FFT on columns (1D FFT along Y axis, but data is transposed)
- **Runs**: Once per cascade per frame (same shader as #4, different data)
- **Completes**: 2D FFT (row FFT → transpose → row FFT = 2D FFT)

### 7. **fft_unpack.glsl** - Generate Final Maps
- **Purpose**: Unpacks IFFT results and creates displacement/normal maps with foam
- **Runs**: Once per cascade per frame
- **Input**: `FFTBuffer.data[]` (final FFT output)
- **Output**:
  - `displacement_map` (image2DArray rgba16f) - xyz displacement
  - `normal_map` (image2DArray rgba16f) - xy gradient, z = dhx/dx, w = foam
- **Foam calculation**:
  - Jacobian = (1 + dhx/dx)(1 + dhz/dz) - (dhz/dx)²
  - Foam grows when Jacobian < threshold (wave breaking)
  - Foam decays exponentially over time
- **ifftshift**: Multiplies by (-1)^(x+y) to shift zero-frequency to center

## Data Flow

```
[1. spectrum_compute]
    ↓ (once, or on parameter change)
spectrum texture (H0(k), H0(-k))
    ↓
[2. spectrum_modulate] (every frame)
    ↓ (time evolution + gradient calculation)
FFTBuffer (4 packed spectra)
    ↓
[4. fft_compute] (horizontal FFT)
    ↓
FFTBuffer (ping-pong to output layer)
    ↓
[5. transpose]
    ↓
FFTBuffer (transposed, ping-pong back to input layer)
    ↓
[4. fft_compute] (vertical FFT, reuses same shader)
    ↓
FFTBuffer (final IFFT result)
    ↓
[7. fft_unpack]
    ↓
displacement_map + normal_map (with foam)
```

## Key Differences from Current OpenMW Implementation

| Feature | GodotOceanWaves | Current OpenMW |
|---------|-----------------|----------------|
| **Spectrum** | JONSWAP/TMA | Phillips |
| **FFT Algorithm** | Stockham | Cooley-Tukey |
| **Shaders** | 6 compute shaders | 4 compute shaders |
| **Transpose** | Yes (explicit shader) | No |
| **GLSL Version** | 460 | 440 |
| **Storage** | SSBOs (std430) | Images (rgba32f) |
| **Push Constants** | Yes | No (uses uniforms) |
| **Foam** | Jacobian in unpack shader | Separate shader |
| **Data Packing** | 4 spectra packed cleverly | Separate textures |

## Adaptation Strategy for OpenMW

### Changes Needed:

1. **GLSL Version**: 460 → 440 (OpenMW constraint)
2. **Push Constants → Uniforms**: Replace all push constants with uniform blocks
3. **Storage Buffers → Textures**: Convert SSBOs to image2D/sampler2D (OSG limitation)
4. **Bindings**: Adjust from Godot's `set=N, binding=M` to OSG's `binding=N`
5. **Remove `#[compute]`**: OSG doesn't use this Godot directive
6. **Local Size**: May need adjustment for performance
7. **Butterfly Precomputation**: Need to call butterfly shader during initialization

### Benefits of Using Godot Shaders:

✅ **Better spectrum** - TMA supports shallow water
✅ **More efficient FFT** - Stockham is ~10% faster
✅ **Cleaner packing** - 4 spectra in one buffer
✅ **Better foam** - Integrated Jacobian calculation
✅ **Proven implementation** - Known to work well
✅ **Future-proof** - Matches reference implementation

## Implementation Plan

1. Convert each shader to OpenMW format (.comp files)
2. Update OceanFFTSimulation to use new 6-shader pipeline
3. Replace image arrays with separate textures (per cascade)
4. Implement ping-pong buffering for FFT
5. Test and tune parameters
