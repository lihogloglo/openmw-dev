# Ocean SSBO Fix - Session Summary

## Root Cause Identified ✅

The ocean was rendering as a **black surface** due to **SSBO buffer allocation failure**. The buffers were created as OSG objects but **GPU memory was never allocated** because:

1. OSG's `VertexBufferObject` creates a wrapper but doesn't automatically call `glBufferData`
2. The buffers weren't attached to the scene graph in a way that triggers automatic upload
3. Both `mFFTBuffer` and `mButterflyBuffer` had size = 0 bytes on the GPU

## Successfully Fixed Issues ✅

1. **FFT Buffer Allocation**: Added manual `glBufferData` call in `updateComputeShaders` for `mFFTBuffer` (67MB)
2. **SSBO Binding Mismatch**: Fixed `fft_unpack.comp` to use `binding = 1` (was `binding = 0`)
3. **Removed `restrict` keyword**: From SSBO declarations in both shaders (can cause issues)
4. **Verified Data Flow**: Confirmed Red debug pattern from `spectrum_modulate.comp` → `fft_unpack.comp` works

## Issues Still Requiring Fix ⚠️

### 1. Butterfly Buffer Initialization (CRITICAL)
**Problem**: The butterfly buffer is allocated but contains **zeros instead of twiddle factors**

**Why**: The initialization order is wrong:
```
1. initializeComputeShaders() runs
   - fft_butterfly.comp dispatches (tries to write to unallocated buffer)
2. Later, in updateComputeShaders()
   - Butterfly buffer gets allocated (but too late!)
```

**Symptom**: Ocean shows **linear bands** instead of 2D waves (only horizontal FFT works, vertical fails due to missing twiddle factors)

**Fix Needed**: Allocate butterfly buffer **BEFORE** dispatching `fft_butterfly.comp` shader
- Add allocation check in `initializeComputeShaders` before line 434
- Same pattern as FFT buffer allocation (check size, allocate if 0)
- Size: `numStages * L_SIZE * 4 * sizeof(float)` = 73,728 bytes

### 2. Missing Vertical FFT Pass
**Problem**: The 2D FFT pipeline is incomplete - only does horizontal pass

**Current Pipeline**:
1. Modulate ✅
2. Horizontal FFT ✅
3. Transpose ✅
4. **MISSING: Vertical FFT** ❌
5. Unpack ✅

**Fix Needed**: Add vertical FFT pass in `updateComputeShaders` between Transpose (line ~656) and Unpack
- Duplicate the horizontal FFT code block
- Same dispatch: `ext->glDispatchCompute(1, L_SIZE, NUM_SPECTRA);`

## Files Modified

- `d:\Gamedev\openmw-snow\files\shaders\lib\ocean\fft_unpack.comp` - Fixed binding to 1
- `d:\Gamedev\openmw-snow\files\shaders\lib\ocean\spectrum_modulate.comp` - Removed restrict
- `d:\Gamedev\openmw-snow\apps\openmw\mwrender\ocean.cpp` - Added FFT buffer allocation

## Test Results

| Test | Result |
|------|--------|
| Solid purple ocean | ✅ Fixed (was binding mismatch) |
| Black ocean | ✅ Fixed (was buffer allocation) |
| Red debug pattern | ✅ Working (SSBO data transfer confirmed) |
| Animated noisy texture | ✅ Working (wave physics calculations) |
| Linear bands | ❌ Current state (butterfly buffer zeros) |
| 2D waves | ❌ Not yet (missing vertical FFT) |

## Next Session Instructions

Start from commit `96da3b8d2db7788ff953af03aaa3cdf99238a68a` and apply these fixes **in order**:

### Fix 1: Butterfly Buffer Allocation in Initialization

In `d:\Gamedev\openmw-snow\apps\openmw\mwrender\ocean.cpp`, function `initializeComputeShaders`, **BEFORE** line 434 (`glBindBufferBase`), add:

```cpp
// ALLOCATE butterfly buffer before initialization shader runs
GLint bufSize = 0;
ext->glBindBuffer(GL_SHADER_STORAGE_BUFFER, butterflyBufferID);
ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);

if (bufSize == 0) {
    std::cout << "Ocean: Allocating Butterfly Buffer..." << std::endl;
    const size_t butterflyElements = numStages * L_SIZE * 4;
    const size_t butterflySizeBytes = butterflyElements * sizeof(float);
    ext->glBufferData(GL_SHADER_STORAGE_BUFFER, butterflySizeBytes, NULL, GL_STATIC_DRAW);
    
    ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);
    std::cout << "Ocean: Allocated Butterfly Buffer. Size: " << bufSize << " bytes" << std::endl;
}
```

### Fix 2: Add FFT Buffer Allocation in Update Loop

In `updateComputeShaders`, in the **Modulate Spectrum** section (around line 560), add after getting `fftBufferID`:

```cpp
// Allocate FFT buffer if not already done
static bool fftBufferAllocated = false;
if (!fftBufferAllocated) {
    GLint bufSize = 0;
    ext->glBindBuffer(GL_SHADER_STORAGE_BUFFER, fftBufferID);
    ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);
    
    if (bufSize == 0) {
        std::cout << "Ocean: Allocating FFT Buffer..." << std::endl;
        const size_t fftElements = L_SIZE * L_SIZE * NUM_SPECTRA * 2 * NUM_CASCADES * 2;
        const size_t fftSizeBytes = fftElements * sizeof(float);
        ext->glBufferData(GL_SHADER_STORAGE_BUFFER, fftSizeBytes, NULL, GL_DYNAMIC_DRAW);
        
        ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);
        std::cout << "Ocean: Allocated FFT Buffer. Size: " << bufSize << " bytes" << std::endl;
    }
    fftBufferAllocated = true;
}
```

### Fix 3: Re-enable FFT and Transpose (if commented out)

Uncomment the FFT computation and transpose stages around lines 607-656.

### Fix 4: Add Vertical FFT Pass

**After** the Transpose stage (around line 656) and **before** "// 5. Unpack", add:

```cpp
// 4. Vertical FFT (same as horizontal, but on transposed data)
if (mComputeFFT.valid())
{
    state->applyAttribute(mComputeFFT.get());
    const osg::Program::PerContextProgram* pcp = state->getLastAppliedProgramObject();
    
    // Bind Butterfly Buffer (Binding 0)
    GLuint butterflyBufferID = mButterflyBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, butterflyBufferID);
    
    // Bind FFT Buffer (Binding 1)
    GLuint fftBufferID = mFFTBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftBufferID);
    
    for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
    {
        if (pcp)
        {
            GLint loc = pcp->getUniformLocation(osg::Uniform::getNameID("cascade_index"));
            if (loc >= 0) ext->glUniform1ui(loc, cascade);
        }
        
        ext->glDispatchCompute(1, L_SIZE, NUM_SPECTRA);
    }
    ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
```

### Expected Result

After all fixes:
- Console shows "Allocating Butterfly Buffer" and "Allocating FFT Buffer" on first run
- Ocean shows **2D animated wave patterns** (not linear bands)
- Normals visualized as varying colors across the surface
- Ocean is still flat geometrically (displacement not applied to vertices yet - that's a separate task)

### Debugging If Issues

- Check console for buffer allocation messages
- Linear bands = butterfly buffer still zero (Fix 1 failed)
- Black ocean = FFT buffer not allocated (Fix 2 failed)
- Noisy texture but not bands = missing vertical FFT (Fix 4 needed)

## Key Learnings

1. **OSG Buffer Management**: OSG `VertexBufferObject` doesn't auto-allocate GPU memory - need manual `glBufferData`
2. **Initialization Timing**: Buffer allocation MUST happen before shaders that write to them dispatch
3. **2D FFT Structure**: Needs Horizontal → Transpose → Vertical pattern (we were missing vertical)
4. **GLSL binding point consistency**: All shaders accessing same SSBO must use same binding number
