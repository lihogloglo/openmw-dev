# Ocean Pitch Black Debug Guide

**Issue:** Ocean rendering completely black after implementing PBR shading

---

## Quick Diagnostic Steps

Follow these steps in order to identify the problem:

### Step 1: Test Basic Rendering
Uncomment line 302 in [ocean.frag](files/shaders/compatibility/ocean.frag:302):
```glsl
gl_FragData[0] = vec4(albedo, 1.0); applyShadowDebugOverlay(); return;
```

**Expected:** Ocean should show blue/white color (mix of water and foam)
- ✅ If you see blue/white: The problem is in the lighting calculations
- ❌ If still black: The problem is earlier (normal sampling or basic setup)

---

### Step 2: Check Individual Lighting Components

If Step 1 works, uncomment ONE of these debug lines at a time (around line 310-318):

#### 2a. Check Ambient Light
```glsl
gl_FragData[0] = vec4(gl_LightModel.ambient.xyz, 1.0);
```
**Expected:** Should show the scene's ambient light color
- If **black**: OpenMW ambient light is zero → this is why ocean is black
- If **visible**: Ambient is working

#### 2b. Check Sun Color
```glsl
gl_FragData[0] = vec4(sunColor, 1.0);
```
**Expected:** Should show sun color (yellowish/white during day, black at night)
- If **black during daytime**: Sun color is not being retrieved correctly
- If **yellow/white**: Sun color is working

#### 2c. Check Diffuse Term
```glsl
gl_FragData[0] = vec4(diffuse, 1.0);
```
**Expected:** Should show ocean lit by sun (brighter where sun hits)
- If **black**: Diffuse calculation is broken
- If **visible**: Diffuse is working

#### 2d. Check Specular Term
```glsl
gl_FragData[0] = vec4(specular, 1.0);
```
**Expected:** Should show bright highlights where sun reflects
- If **black**: Specular calculation is broken or GGX issue
- If **visible specks**: Specular is working

#### 2e. Check Shadow Value
```glsl
gl_FragData[0] = vec4(vec3(shadow), 1.0);
```
**Expected:** Should show white (1.0) in sunlight, darker in shadows
- If **black (0.0)**: Ocean is in complete shadow → fix shadow system
- If **gray/white**: Shadow system working

#### 2f. Check Normals
```glsl
gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0);
```
**Expected:** Should show colorful wavy pattern (RGB = XYZ of normal)
- If **flat blue (0.5, 1.0, 0.5)**: Normals are flat → gradient sampling issue
- If **black**: Normal calculation is completely broken
- If **wavy colors**: Normals are working

---

## Most Likely Causes

Based on typical issues:

### 1. **Ambient Light is Zero** (MOST LIKELY)
**Symptom:** Ocean works during day but black at night
**Cause:** `gl_LightModel.ambient.xyz` returns vec3(0.0)
**Solution:** The MIN_AMBIENT fix I added should help, but you might need to increase it:
```glsl
const vec3 MIN_AMBIENT = vec3(0.3) * WATER_COLOR; // Increased from 0.1
```

### 2. **Sun Color is Zero**
**Symptom:** Ocean black during day too
**Cause:** `lcalcDiffuse(0)` returns vec3(0.0)
**Test:** Check with debug line 2b above
**Solution:** May need to use different lighting function or add fallback

### 3. **Shadow Value is Zero**
**Symptom:** Ocean always black
**Cause:** `unshadowedLightRatio(linearDepth)` returns 0.0
**Test:** Check with debug line 2e above
**Solution:** May need to disable shadow for ocean or use different shadow function

### 4. **Normals are Inverted/Wrong**
**Symptom:** Ocean visible but weirdly dark
**Cause:** Normal pointing wrong direction
**Test:** Check with debug line 2f above
**Solution:** Might need to flip normal Y component

---

## Quick Fixes to Try

### Fix #1: Increase Minimum Ambient
Edit [ocean.frag:295](files/shaders/compatibility/ocean.frag:295):
```glsl
const vec3 MIN_AMBIENT = vec3(0.5) * WATER_COLOR; // Much brighter
```

### Fix #2: Add Sun Color Fallback
Edit around [ocean.frag:239](files/shaders/compatibility/ocean.frag:239):
```glsl
vec3 sunColor = lcalcDiffuse(0); // Sun color and intensity

// Fallback if sun color is zero
if (dot(sunColor, sunColor) < 0.001) {
    sunColor = vec3(1.0); // White light fallback
}
```

### Fix #3: Temporarily Disable PBR (Quick Test)
Replace the entire PBR section (lines 224-301) with the old simple lighting:
```glsl
// TEMPORARY: Simple lighting to test if PBR is the issue
const vec3 FOAM_COLOR = vec3(0.95, 0.95, 0.95);
vec3 baseColor = mix(WATER_COLOR, FOAM_COLOR, smoothstep(0.0, 1.0, foam));
float upFacing = max(0.0, normal.y);
vec3 finalColor = baseColor * (0.3 + upFacing * 0.7);
float alpha = mix(0.85, 0.95, foam);
gl_FragData[0] = vec4(finalColor, alpha);
```
If this works, the PBR calculations have a bug.

---

## Report Back

After trying these steps, let me know:
1. Which debug visualization worked (if any)
2. What values you saw (e.g., "ambient light is black", "sun color is yellow", etc.)
3. Whether any of the quick fixes helped

This will help me identify the exact problem!

---

## Notes

- The old water shader ([water.frag](files/shaders/compatibility/water.frag)) had similar lighting setup and worked
- Make sure you're testing during daytime in-game (sun should be visible)
- The ocean was visible before adding PBR, so the issue is definitely in the lighting calculations
