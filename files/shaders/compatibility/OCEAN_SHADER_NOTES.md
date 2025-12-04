# Ocean Shader Debug Log

## Current Test: Finding where black screen starts

### Test 1: Red output after refraction sampling
**Location:** Line 474 (after `sampleRefractionMap`)
**Result:** RED - shader runs to this point

### Test 2: Output refraction before absorption
**Location:** Line 477
**Code:** `gl_FragData[0] = vec4(refraction, 1.0); return;`
**Result:** ???

### Test 3: Output refraction AFTER absorption
**Location:** Line 501
**Code:** `gl_FragData[0] = vec4(refraction, 1.0); return;`
**Result:** ???

### Test 4: Check cameraPos.z value
**Location:** Line 480
**Code:** `gl_FragData[0] = vec4(cameraPos.z > 0.0 ? 1.0 : 0.0, 0.0, 0.0, 1.0); return;`
**Result:** ???

### Test 5: Check waterDepthDistorted
**Location:** Line 483
**Code:** `gl_FragData[0] = vec4(vec3(waterDepthDistorted / 1000.0), 1.0); return;`
**Result:** ???

---

## Debug Strategy
1. Move debug output progressively later in the shader
2. When it goes black, we found the problem area
3. Then examine that specific code

## Quick Reference - Debug Lines in ocean.frag
- Line 474: Red test (WORKS)
- Line 477: Refraction before absorption
- Line 480: cameraPos.z check
- Line 483: waterDepthDistorted check
- Line 501: Refraction after absorption
