# GodotOceanWaves Port Status

## ✅ Completed: Shader Conversion

All 6 GodotOceanWaves compute shaders have been successfully ported to OpenMW format (GLSL 440).

### Converted Shaders

1. **`spectrum_compute.comp`** - JONSWAP/TMA spectrum generation
   - Generates initial wave spectrum H0(k) and H0*(-k)
   - Uses TMA spectrum (better shallow water model than Phillips)
   - Includes Hasselmann directional spreading
   - Output: `image2D` (rgba16f)

2. **`spectrum_modulate.comp`** - Time evolution & gradient calculation
   - Modulates spectrum based on dispersion relation
   - Calculates displacement (hx, hy, hz) and gradients
   - Packs 4 spectra into SSBO for FFT
   - Input: `image2D`, Output: SSBO `vec2 data[]`

3. **`fft_butterfly_factors.comp`** - Precompute butterfly factors
   - Runs once at initialization
   - Generates twiddle factors for Stockham FFT
   - Output: SSBO `vec4 butterfly[]` (log2(N) × N)

4. **`fft_stockham.comp`** - Stockham FFT kernel (row/column passes)
   - Coalesced decimation-in-time FFT
   - Uses shared memory for performance
   - Ping-pong buffering within SSBO
   - Processes one row at a time

5. **`fft_transpose.comp`** - Matrix transpose
   - Required between row and column FFT passes
   - Uses 32×32 tiled shared memory for coalescing
   - Avoids bank conflicts with padding

6. **`fft_unpack.comp`** - Generate final displacement/normal/foam maps
   - Unpacks IFFT results
   - Calculates Jacobian for foam
   - Applies ifftshift
   - Outputs: displacement map + normal map with foam

## Key Changes from Original

| Aspect | GodotOceanWaves | OpenMW Port |
|--------|-----------------|-------------|
| GLSL Version | 460 | 440 |
| Godot Directives | `#[compute]` | Removed |
| Push Constants | Yes | Converted to uniforms |
| Bindings | `set=N, binding=M` | `binding=N` |
| SSBOs | `std430` | Kept (OpenMW supports) |
| Image Arrays | `image2DArray` | Single `image2D` per cascade |
| Cascade Handling | Z-dimension | Separate dispatches |

## 🔧 TODO: C++ Implementation

The C++ code in `oceanfftsimulation.cpp` needs significant updates to use the new 6-shader pipeline:

### Required Changes

#### 1. **Add SSBO Support**
```cpp
// Replace image-based FFT buffers with SSBOs
osg::ref_ptr<osg::BufferObject> mFFTBuffer;
osg::ref_ptr<osg::BufferObject> mButterflyBuffer;
```

#### 2. **New Shader Pipeline**
Old pipeline (4 shaders):
```
spectrum_update → butterfly_fft → displacement → foam
```

New pipeline (6 shaders):
```
spectrum_compute (once)
    ↓
spectrum_modulate (every frame)
    ↓
fft_butterfly_factors (once)
    ↓
fft_stockham (horizontal)
    ↓
fft_transpose
    ↓
fft_stockham (vertical)
    ↓
fft_unpack
```

#### 3. **Initialization Changes**

**Old:**
- Generate spectrum on CPU (Phillips)
- Create butterfly texture on CPU
- Upload to GPU textures

**New:**
- Dispatch `spectrum_compute` shader (GPU)
- Dispatch `fft_butterfly_factors` shader (GPU)
- Create SSBOs for FFT data

#### 4. **Per-Frame Dispatch Sequence**

```cpp
void OceanFFTSimulation::dispatchCompute(osg::State* state)
{
    for (auto& cascade : mCascades)
    {
        // 1. Modulate spectrum
        dispatch(spectrum_modulate);

        // 2. Horizontal FFT (all 4 spectra)
        for (int i = 0; i < 4; ++i) {
            dispatch(fft_stockham, spectrum=i);
        }

        // 3. Transpose (all 4 spectra)
        for (int i = 0; i < 4; ++i) {
            dispatch(fft_transpose, spectrum=i);
        }

        // 4. Vertical FFT (all 4 spectra)
        for (int i = 0; i < 4; ++i) {
            dispatch(fft_stockham, spectrum=i);
        }

        // 5. Unpack results
        dispatch(fft_unpack);
    }
}
```

#### 5. **SSBO Buffer Sizes**

```cpp
// FFT Buffer: map_size × map_size × NUM_SPECTRA(4) × 2 (ping-pong) × sizeof(vec2)
size_t fftBufferSize = resolution * resolution * 4 * 2 * sizeof(float) * 2;

// Butterfly Buffer: log2(map_size) × map_size × sizeof(vec4)
size_t butterflyBufferSize = log2(resolution) * resolution * sizeof(float) * 4;
```

#### 6. **Uniform Bindings**

Each shader needs different uniforms:

**spectrum_compute:**
- `uSeed`, `uTileLength`, `uAlpha`, `uPeakFrequency`
- `uWindSpeed`, `uAngle`, `uDepth`, `uSwell`, `uDetail`, `uSpread`

**spectrum_modulate:**
- `uTileLength`, `uDepth`, `uTime`, `uMapSize`

**fft_stockham & fft_transpose:**
- `uMapSize`, `uSpectrumIndex` (0-3)

**fft_unpack:**
- `uMapSize`, `uWhitecap`, `uFoamGrowRate`, `uFoamDecayRate`

## Performance Considerations

### Improvements Over Old Implementation
- ✅ **Better spectrum**: JONSWAP/TMA vs Phillips
- ✅ **Correct 2D FFT**: Includes transpose step
- ✅ **Coalesced access**: Shared memory optimizations
- ✅ **SSBOs**: More efficient than ping-pong textures
- ✅ **Packed data**: 4 spectra in one buffer

### Potential Bottlenecks
- ⚠️ **More dispatch calls**: 6 shaders vs 4 (but each is faster)
- ⚠️ **SSBO size**: Larger memory footprint
- ⚠️ **Cascade loop**: Need to process each cascade separately

## Testing Strategy

1. **Shader Compilation Test**
   - Load shaders and check for GL errors
   - Verify SSBO support (GL 4.3+)

2. **Single Cascade Test**
   - Start with one cascade, 256×256
   - Verify spectrum generation
   - Check FFT output values

3. **Multiple Cascades**
   - Test with 2-3 cascades
   - Verify tile size scaling

4. **Visual Validation**
   - Compare with GodotOceanWaves reference
   - Check for artifacts, NaNs, or black ocean

## Next Steps

1. Back up current `oceanfftsimulation.cpp/hpp`
2. Implement SSBO creation and binding
3. Update shader loading to use new 6 shaders
4. Implement new dispatch sequence
5. Test with verbose logging
6. Debug and iterate

## Reference Documentation

- Original: [GodotOceanWaves](https://github.com/2Retr0/GodotOceanWaves)
- Stockham FFT: http://wwwa.pikara.ne.jp/okojisan/otfft-en/stockham3.html
- Tessendorf Paper: "Simulating Ocean Water"
- Horvath Paper: "Empirical Directional Wave Spectra for Computer Graphics"
