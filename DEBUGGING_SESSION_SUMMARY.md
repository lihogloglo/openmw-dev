# Snow Deformation Debugging Session - Complete Summary

## Problem Statement
Snow deformation system logs show footprints are being created and rendered, but **nothing appears on screen**. No visual deformation, no trails, nothing.

## What We've Confirmed Working ✅

### 1. **Footprint Tracking System** ✅
- Logs show: `"SnowDeformation: Added footprint at (x,y) intensity=1"`
- Footprints are being created as player moves
- Footprint data structure is populated correctly

### 2. **RTT System Claims to Render** ✅
- Logs show: `"SnowDeformation: renderFootprintsToTexture - rendering X footprints to RTT camera"`
- Logs show: `"SnowDeformation: Added X footprint quads to RTT camera"`
- Code is executing the render path

### 3. **Shader Integration** ✅
- Logs show: `"Terrain material: Snow deformation ENABLED for chunk - texture=valid, strength=5"`
- The `@snowDeformation` define is being set
- Shaders are compiling successfully (after fixing variable name conflict)

### 4. **Fragment Shader Execution** ✅
- Terrain appears **reddish** from the constant `vec3(0.2, 0.0, 0.0)` tint
- This proves the `@snowDeformation` block in terrain.frag is running

### 5. **Varying Data Flow** ✅
- When we added `debugDeformation = deformation + 0.5` (forced non-zero value)
- Terrain turned **BRIGHT RED-ORANGE** everywhere
- This proves data passes from vertex shader → fragment shader correctly

### 6. **Coordinate Transform to World Space** ✅
- Fixed: Was using `gl_ModelViewMatrix * gl_Vertex` (view space)
- Now using: `osg_ViewMatrixInverse * viewPos` (world space)
- Shader compiles and runs

## What We've Confirmed NOT Working ❌

### 1. **Texture Sampling Returns Zero** ❌
- Test: Sampled texture center `vec2(0.5, 0.5)` with `* 100.0` multiplier
- Result: Terrain stays **just reddish** (same as constant tint)
- Conclusion: **Texture is completely empty/black**

### 2. **RTT Cameras Not Actually Rendering** ❌
- Despite logs saying footprints are added to cameras
- Despite cameras being created and configured
- **The texture has no data in it**

## Root Cause Analysis

The RTT (Render-To-Texture) cameras are **not actually rendering** to the deformation texture, even though:
- Cameras are created with `PRE_RENDER` order
- Footprint quads are added as children
- Cameras are attached to the scene graph
- Logs claim rendering is happening

**Why RTT might not be rendering:**

### Possible Issue #1: Texture Not Properly Allocated
**Status:** ATTEMPTED TO FIX ✅
- Added `setImage()` calls with allocated osg::Image
- Created backing memory for both textures
- **Still doesn't work**

### Possible Issue #2: Camera Node Mask
**Status:** ATTEMPTED TO FIX ✅
- Changed from `Mask_RenderToTexture` to `~0` (render always)
- **Still doesn't work**

### Possible Issue #3: Cameras Not in Render Traversal
**Status:** NOT YET INVESTIGATED ⚠️
- Cameras might not be visited during scene graph traversal
- OSG might be culling them or not executing PRE_RENDER cameras
- Parent node might have wrong mask

### Possible Issue #4: FBO/Attachment Issues
**Status:** NOT YET INVESTIGATED ⚠️
- `attach(COLOR_BUFFER0, texture)` might not be working
- FBO might not be complete
- Texture format (GL_R16F) might not be supported

### Possible Issue #5: Shader Not Running on RTT Geometry
**Status:** NOT YET INVESTIGATED ⚠️
- Decay shader (`snow_decay`) might have errors
- Footprint shader (`snow_footprint`) might have errors
- Geometry might not have proper attributes

## Key Code Locations

### Texture Creation
**File:** `apps/openmw/mwrender/snowdeformation.cpp:67-105`
```cpp
// Creates two 1024x1024 GL_R16F textures with allocated Image data
mDeformationTexture->setImage(image);  // Front buffer
mDeformationTextureBack->setImage(imageBack);  // Back buffer for ping-pong
```

### Camera Setup
**File:** `apps/openmw/mwrender/snowdeformation.cpp:107-163`
```cpp
// Decay camera (order 0)
mDecayCamera->setRenderOrder(osg::Camera::PRE_RENDER, 0);
mDecayCamera->attach(COLOR_BUFFER0, mDeformationTextureBack.get());
mDecayCamera->setNodeMask(~0);  // Render always

// Footprint camera (order 1)
mDeformationCamera->setRenderOrder(osg::Camera::PRE_RENDER, 1);
mDeformationCamera->attach(COLOR_BUFFER0, mDeformationTextureBack.get());
mDeformationCamera->setNodeMask(~0);  // Render always
```

### Footprint Rendering
**File:** `apps/openmw/mwrender/snowdeformation.cpp:513-553`
```cpp
// Creates quads for each footprint and adds to mDeformationCamera
for (const auto& footprint : mFootprints) {
    osg::ref_ptr<osg::Geometry> quad = createFootprintQuad(footprint);
    // Apply snow_footprint shader
    mDeformationCamera->addChild(quad);
}
```

### Texture Sampling (Debug Version)
**File:** `files/shaders/compatibility/terrain.vert:22-38`
```glsl
float sampleDeformation(vec2 worldPos) {
    // Currently sampling CENTER of texture (0.5, 0.5)
    vec4 texSample = texture2D(deformationMap, vec2(0.5, 0.5));
    return texSample.r * 100.0;  // Amplify for visibility
}
```

### Debug Visualization
**File:** `files/shaders/compatibility/terrain.frag:99-112`
```glsl
// Constant red tint (proves shader runs)
gl_FragData[0].xyz += vec3(0.2, 0.0, 0.0);

// Show deformation value
if (debugDeformation > 0.0001) {
    gl_FragData[0].xyz = vec3(1.0, debugDeformation, 0.0);  // Bright red-orange
}
```

## Current State

### Shaders
- ✅ Vertex shader: Samples texture center, passes to fragment
- ✅ Fragment shader: Shows red tint (proves it runs), would show bright if deformation > 0
- ✅ Compiling successfully
- ✅ `@snowDeformation` define is set correctly

### Textures
- ✅ Created with proper format (GL_R16F)
- ✅ Have allocated Image data
- ✅ Attached to RTT cameras
- ✅ Bound to terrain shader (texture unit 4)
- ❌ **Completely empty (all zeros)**

### Cameras
- ✅ Created with PRE_RENDER order
- ✅ Attached to scene graph
- ✅ Have geometry children (footprint quads)
- ✅ Node mask set to ~0
- ❌ **Not actually rendering to texture**

### Footprints
- ✅ Being created as player moves
- ✅ Added to camera as geometry
- ✅ Have shader applied
- ❌ **Not appearing in texture**

## Next Steps to Investigate

### 1. Check if RTT Cameras are Being Traversed
Add logging to verify cameras are actually visited during render:
```cpp
// In createDeformationCamera(), add a callback
struct CameraCallback : public osg::NodeCallback {
    virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
        Log(Debug::Info) << "RTT Camera callback executed!";
        traverse(node, nv);
    }
};
mDeformationCamera->addCullCallback(new CameraCallback());
```

### 2. Verify FBO Completeness
Check if the framebuffer is complete:
```cpp
// After camera creation
GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
if (status != GL_FRAMEBUFFER_COMPLETE) {
    Log(Debug::Error) << "FBO incomplete: " << status;
}
```

### 3. Test with Simple Color Output
Replace footprint shader with trivial shader that just outputs red:
```glsl
// snow_footprint.frag
void main() {
    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);  // Just output red
}
```

### 4. Check Shader Errors
Verify footprint and decay shaders compile:
```cpp
// After getting shader program
if (!footprintProgram) {
    Log(Debug::Error) << "Footprint shader failed to compile!";
}
```

### 5. Simplify Camera Setup
Try minimal RTT camera setup:
```cpp
// Remove decay pass entirely
// Use single camera with FRAME_BUFFER_OBJECT
// Just render one red quad to texture
```

### 6. Check Texture Format Support
GL_R16F might not be supported on all hardware:
```cpp
// Try GL_RGBA instead
mDeformationTexture->setInternalFormat(GL_RGBA);
```

### 7. Verify Geometry Has Proper Attributes
Check footprint quads:
```cpp
Log(Debug::Info) << "Footprint quad vertices: " << quad->getVertexArray()->getNumElements();
Log(Debug::Info) << "Footprint quad primitives: " << quad->getNumPrimitiveSets();
```

### 8. Force Texture Clear to Non-Zero
Test if texture writes work at all:
```cpp
// Fill texture with test pattern
float* data = (float*)image->data();
for (int i = 0; i < DEFORMATION_TEXTURE_SIZE * DEFORMATION_TEXTURE_SIZE; i++) {
    data[i] = 1.0f;  // Fill with white
}
image->dirty();  // Mark as needing upload
```
If this makes terrain bright, texture binding works but RTT doesn't.

### 9. Check OpenMW's RTT Examples
Look at how other RTT cameras work in OpenMW:
- Water reflection cameras
- Shadow map cameras
- Any post-processing effects

Find similar code and copy its setup exactly.

### 10. Enable OSG Debugging
Set environment variables:
```bash
OSG_NOTIFY_LEVEL=DEBUG
OSG_GL_ERROR_CHECKING=ON
```
Run OpenMW and check for GL errors or warnings.

## Summary of Fixes Applied

1. ✅ Fixed coordinate transform (view space → world space)
2. ✅ Fixed shader compilation error (variable name conflict)
3. ✅ Added texture image allocation (`setImage()`)
4. ✅ Changed camera node masks to ~0
5. ✅ Increased footprint size and deformation strength (10x)
6. ✅ Increased texture coverage area (300 → 4096 units)
7. ✅ Added comprehensive debug logging
8. ✅ Added visual debug overlays (red tint, bright red for deformation)

## Summary of What Still Doesn't Work

**The RTT cameras are not writing to the deformation texture.**

Everything else works:
- Footprints tracked ✅
- Cameras created ✅
- Shaders run ✅
- Texture bound ✅
- Data flows ✅

But the ONE critical piece—**RTT rendering to populate the texture**—is not happening.

## Files Modified

### Shaders
- `files/shaders/compatibility/terrain.vert` - Added deformation sampling, debug output
- `files/shaders/compatibility/terrain.frag` - Added red debug visualization

### Core System
- `apps/openmw/mwrender/snowdeformation.cpp` - Added texture allocation, camera fixes, logging
- `apps/openmw/mwrender/snowdeformation.hpp` - Increased scale constants

### Integration
- `components/terrain/material.cpp` - Added logging

## Diagnostic Strategy for Next Session

1. **First**: Verify cameras are in render traversal (add callback)
2. **Second**: Test if texture writes work at all (fill with test pattern)
3. **Third**: Simplify to absolute minimum RTT setup
4. **Fourth**: Copy working RTT code from elsewhere in OpenMW
5. **Fifth**: Check shader compilation for footprint/decay shaders

The issue is 100% in the RTT rendering path. Everything else is proven working.

## Key Insight

When we forced `debugDeformation = deformation + 0.5`, ALL terrain turned bright red-orange. This proves:
- ✅ Shader code executes
- ✅ Varying passes data
- ✅ Fragment shader can display colors
- ✅ The visualization system works perfectly

When we sample the texture, it returns 0.0 every time. This proves:
- ❌ Texture is empty
- ❌ RTT is not rendering
- ❌ Something is wrong with the camera setup or render traversal

**The bug is NOT in the shaders, NOT in the coordinate math, NOT in the visualization. The bug is in getting OSG to actually execute the RTT cameras.**

---

## Quick Reference: Current Debug Settings

```cpp
// Texture coverage
DEFAULT_WORLD_TEXTURE_SIZE = 4096.0f;  // 58 meters

// Footprint parameters
footprint.radius = 80.0f;    // Very large
footprint.intensity = 1.0f;  // Maximum
mDeformationStrength = 5.0f; // 10x normal

// Sampling (in shader)
texture2D(deformationMap, vec2(0.5, 0.5)).r * 100.0;  // Center, amplified

// Visual indicators
vec3(0.2, 0.0, 0.0)  // Constant red tint (proves shader runs)
vec3(1.0, deformation, 0.0)  // Bright if deformation > 0
```

All these are EXTREME values for maximum visibility. Once RTT works, dial them back to realistic values.
