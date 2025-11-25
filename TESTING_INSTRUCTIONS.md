# SSR + Cubemap Testing Instructions

## Changes Made for Testing

### 1. Ocean Disabled ([water.cpp:461](apps/openmw/mwrender/water.cpp#L461))
```cpp
mUseOcean = false; // Ocean completely disabled
```

### 2. Old Water Hidden ([water.cpp:977](apps/openmw/mwrender/water.cpp#L977))
```cpp
bool useNewWater = (mLake != nullptr); // Force use of Lake system
```

### 3. Lake Always Enabled ([water.cpp:842](apps/openmw/mwrender/water.cpp#L842))
```cpp
mLake->setEnabled(mEnabled); // Enabled everywhere, not just non-ocean
```

### 4. Lake Uses Water Shader with SSR ([lake.cpp:123](apps/openmw/mwrender/lake.cpp#L123))
```cpp
defineMap["useSSRCubemap"] = "1"; // SSR enabled in Lake shader
```

---

## Current Architecture

### Water Rendering Flow

```
Game Loads
    ↓
WaterManager creates:
    ├─ Ocean (DISABLED)
    ├─ Lake (ENABLED everywhere)
    └─ Legacy water plane (HIDDEN)
    ↓
Lake::initGeometry()
    └─ Creates 100km×100km quad at z=mHeight
    ↓
Lake::initShaders()
    └─ Loads water.frag with useSSRCubemap=1
    ↓
Water Shader Rendering:
    ├─ Samples ssrTexture (unit 5)
    ├─ Samples environmentMap cubemap (unit 6)
    └─ Blends based on SSR confidence
```

---

## What You Should See

### Expected Behavior
✅ **Blue water** everywhere (Lake system)
✅ **Single flat plane** at current water height
✅ **No ocean FFT waves**
❌ **Reflections may not work yet** (textures not bound to Lake)

### Current Limitation
**Problem:** Lake shader uses water.frag but **doesn't bind textures**!

The water.frag shader expects:
- `ssrTexture` (unit 5) → NOT bound by Lake
- `environmentMap` (unit 6) → NOT bound by Lake
- `normalMap` (unit 0) → NOT bound by Lake
- `reflectionMap` (unit 1) → NOT bound by Lake

**Result:** Shader will sample black/undefined textures

---

## Next Steps to Get Reflections Working

### Option A: Quick Fix - Use Simple Lake Shader
Revert Lake to use simple `lake.frag`:
- Solid blue water
- No reflections
- **Fastest way to see SOMETHING**

### Option B: Proper Fix - Bind Textures to Lake
Update `Lake::initShaders()` to bind all required textures:

```cpp
// Bind normal map
osg::ref_ptr<osg::Texture2D> normalMap = /* load normal map */;
stateset->setTextureAttributeAndModes(0, normalMap, osg::StateAttribute::ON);
stateset->addUniform(new osg::Uniform("normalMap", 0));

// Bind SSR texture (from WaterManager)
// Problem: Lake doesn't have access to WaterManager!
```

**Challenge:** Lake needs access to:
- SSRManager (from WaterManager)
- CubemapManager (from WaterManager)
- Reflection RTT (from WaterManager)

This requires architectural changes.

### Option C: Make Lake Inherit from Old Water Logic
Copy texture binding code from `ShaderWaterStateSetUpdater` into Lake.

---

## Recommended Path Forward

### Phase 1: Verify Lake Renders (TODAY)
1. **Build** the project
2. **Run** OpenMW
3. **Expect:** Flat blue/black water everywhere
4. **Verify:** Old water plane is hidden
5. **Verify:** Lake geometry exists

### Phase 2: Add Texture Bindings to Lake
1. **Give Lake access to WaterManager:**
   ```cpp
   Lake::Lake(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
              WaterManager* waterManager) // ADD THIS
   ```

2. **Create Lake StateSetUpdater** (copy from ShaderWaterStateSetUpdater):
   ```cpp
   class LakeStateSetUpdater : public SceneUtil::StateSetUpdater {
       void setDefaults(osg::StateSet* stateset) override {
           // Bind normal map
           // Bind SSR texture
           // Bind cubemap
           // Bind reflection RTT
       }

       void apply(osg::StateSet* stateset, osg::NodeVisitor* nv) override {
           // Update SSR/cubemap textures per frame
       }
   };
   ```

3. **Attach updater to Lake root node**

### Phase 3: Test SSR Visually
Once textures are bound:
- Walk around water
- Look for reflections
- Verify SSR + cubemap blending

---

## Alternative Simpler Approach

Instead of making Lake complex, **just enable the legacy water with SSR**:

1. **Revert Lake changes**
2. **Keep old water plane**
3. **Just add SSR textures to ShaderWaterStateSetUpdater**

The SSR integration you already did in `ShaderWaterStateSetUpdater` should work!

The problem might be that SSR textures aren't being generated because SSRManager isn't initialized properly.

---

## Debugging Checklist

### 1. Verify Lake Geometry Exists
Add debug output to `Lake::initGeometry()`:
```cpp
std::cout << "Lake geometry created: " << mWaterGeom->getNumPrimitiveSets() << " primitives\n";
```

### 2. Verify Shader Compilation
Add debug to `Lake::initShaders()`:
```cpp
if (program) {
    std::cout << "Lake shader program created successfully\n";
} else {
    std::cerr << "ERROR: Failed to create lake shader!\n";
}
```

### 3. Check Node Visibility
```cpp
std::cout << "Lake node mask: " << mRootNode->getNodeMask() << "\n";
std::cout << "Lake enabled: " << mEnabled << "\n";
```

### 4. Verify SSR Textures Exist
In `renderingmanager.cpp` update loop:
```cpp
if (ssrMgr && ssrMgr->getResultTexture()) {
    std::cout << "SSR texture exists\n";
} else {
    std::cerr << "SSR texture is NULL!\n";
}
```

---

## Quick Win: Disable SSR, Test Basic Lake

If SSR is causing issues, test with SSR **disabled**:

**In lake.cpp:**
```cpp
defineMap["useSSRCubemap"] = "0"; // Disable SSR temporarily
```

This will use the old reflection system, which should work immediately.

Then incrementally enable SSR once basic water works.

---

## Summary

**Current State:**
- Ocean: ❌ Disabled
- Lake: ✅ Enabled (but may not render correctly)
- Old Water: ❌ Hidden
- SSR: ❓ Enabled in shader but textures may not be bound

**To Fix:**
1. Verify Lake renders (even if black)
2. Bind textures to Lake shader
3. OR use old water with SSR instead

**Fastest Path to See Something:**
Use old water + SSR instead of Lake!
