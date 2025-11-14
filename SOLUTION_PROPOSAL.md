# Snow Deformation RTT Fix - Solution Proposal

## Problem Summary
RTT cameras execute but don't write to texture. CPU upload works (proven by test pattern), but camera->attach() RTT produces no output.

## Root Cause
Using OSG automatic RTT (camera->attach()) which may not work correctly in this configuration. The ripples system (which works) uses manual FBO management instead.

## SOLUTION 1: Manual FBO Management (RECOMMENDED)

### Implementation Strategy
Convert to ripples-style manual FBO management:

1. **Create FBOs explicitly**
2. **Custom drawable with drawImplementation()**
3. **Manually bind FBO and render**

### Code Changes Required

#### Step 1: Create Custom Drawable

Create `SnowDeformationDrawable` class that inherits from `osg::Geometry`:

```cpp
class SnowDeformationDrawable : public osg::Geometry
{
public:
    void drawImplementation(osg::RenderInfo& renderInfo) const override;

    osg::ref_ptr<osg::FrameBufferObject> mDecayFBO;
    osg::ref_ptr<osg::FrameBufferObject> mFootprintFBO;
    osg::ref_ptr<osg::Texture2D> mFrontTexture;
    osg::ref_ptr<osg::Texture2D> mBackTexture;
    osg::ref_ptr<osg::Program> mDecayProgram;
    osg::ref_ptr<osg::Program> mFootprintProgram;

    // Footprint data updated each frame
    std::vector<Footprint> mFootprints;
    osg::Vec2f mTextureCenter;
    float mWorldTextureSize;
};
```

#### Step 2: Implement drawImplementation

```cpp
void SnowDeformationDrawable::drawImplementation(osg::RenderInfo& renderInfo) const
{
    osg::State& state = *renderInfo.getState();

    // PASS 1: Decay
    state.applyAttribute(mDecayProgram);
    mDecayFBO->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);
    state.applyTextureAttribute(0, mFrontTexture);

    // Render fullscreen quad with decay shader
    osg::Geometry::drawImplementation(renderInfo);

    // PASS 2: Footprints (if any)
    if (!mFootprints.empty())
    {
        state.applyAttribute(mFootprintProgram);
        mFootprintFBO->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);

        // Enable additive blending
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        // Render each footprint quad
        for (const auto& fp : mFootprints)
        {
            // Set uniforms
            // Render quad geometry
        }

        glDisable(GL_BLEND);
    }
}
```

#### Step 3: Camera Setup

```cpp
void SnowDeformationManager::createDeformationCamera()
{
    // Single camera, drawable handles both passes
    mDeformationCamera = new osg::Camera;
    mDeformationCamera->setRenderOrder(osg::Camera::PRE_RENDER);
    mDeformationCamera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
    mDeformationCamera->setNodeMask(Mask_RenderToTexture);
    mDeformationCamera->setClearMask(GL_COLOR_BUFFER_BIT);
    mDeformationCamera->setClearColor(osg::Vec4(0,0,0,0));
    mDeformationCamera->setViewport(0, 0, DEFORMATION_TEXTURE_SIZE, DEFORMATION_TEXTURE_SIZE);
    mDeformationCamera->setImplicitBufferAttachmentMask(0, 0);
    mDeformationCamera->setCullingActive(false);

    // Create drawable
    mDeformationDrawable = new SnowDeformationDrawable(mResourceSystem);

    // Create FBOs
    mDeformationDrawable->mDecayFBO = new osg::FrameBufferObject;
    mDeformationDrawable->mFootprintFBO = new osg::FrameBufferObject;

    // Attach textures to FBOs
    mDeformationDrawable->mDecayFBO->setAttachment(osg::Camera::COLOR_BUFFER0,
        osg::FrameBufferAttachment(mDeformationTextureBack.get()));
    mDeformationDrawable->mFootprintFBO->setAttachment(osg::Camera::COLOR_BUFFER0,
        osg::FrameBufferAttachment(mDeformationTextureBack.get()));

    mDeformationCamera->addChild(mDeformationDrawable);
    mRootNode->addChild(mDeformationCamera);
}
```

### Advantages
- Proven pattern (ripples works)
- Full control over rendering
- Easy debugging
- No reliance on OSG automatic RTT

### Disadvantages
- More code
- Manual state management

---

## SOLUTION 2: Fix Current Approach (SIMPLER BUT RISKY)

Try fixing the current camera->attach() approach with these changes:

### Fix 1: Add Explicit Cull Mask

```cpp
mDecayCamera->setCullMask(0xffffffff);  // See everything
mDeformationCamera->setCullMask(0xffffffff);
```

### Fix 2: Ensure Texture Allocation

```cpp
// Force texture realization
mDeformationTexture->dirtyTextureObject();
mDeformationTextureBack->dirtyTextureObject();

// Or create empty image
osg::ref_ptr<osg::Image> emptyImage = new osg::Image;
emptyImage->allocateImage(DEFORMATION_TEXTURE_SIZE, DEFORMATION_TEXTURE_SIZE, 1, GL_RGBA, GL_FLOAT);
memset(emptyImage->data(), 0, emptyImage->getTotalSizeInBytes());
mDeformationTexture->setImage(emptyImage);
mDeformationTextureBack->setImage(emptyImage->clone(osg::CopyOp::DEEP_COPY_ALL));
```

### Fix 3: Don't Detach/Reattach Every Frame

```cpp
// In updateDeformationTexture(), DON'T detach/attach
// Just swap the texture pointers
std::swap(mDeformationTexture, mDeformationTextureBack);

// Update decay shader input
osg::StateSet* decayState = mDecayQuad->getStateSet();
decayState->setTextureAttributeAndModes(0, mDeformationTexture.get(), osg::StateAttribute::ON);
```

### Fix 4: Explicit Geometry Node Mask

```cpp
// In renderFootprintsToTexture()
for (const auto& footprint : mFootprints)
{
    osg::ref_ptr<osg::Geometry> quad = createFootprintQuad(footprint);
    quad->setNodeMask(~0u);  // Visible to all
    // ... shader setup ...
    mDeformationCamera->addChild(quad);
}

// Same for decay quad
mDecayQuad->setNodeMask(~0u);
```

### Fix 5: Check Shader Compilation

Add explicit shader error checking:

```cpp
osg::ref_ptr<osg::Program> footprintProgram = shaderMgr.getProgram("compatibility/snow_footprint", defines);

if (!footprintProgram)
{
    Log(Debug::Error) << "SnowDeformation: Failed to load footprint shader program!";
    return;
}

// Try to force compilation to see errors
const osg::Program::ShaderList& shaders = footprintProgram->getShaderList();
for (const auto& shader : shaders)
{
    if (!shader->getShaderSource().empty())
    {
        Log(Debug::Info) << "Shader type " << shader->getType() << " has source, length: "
                         << shader->getShaderSource().length();
    }
}
```

---

## SOLUTION 3: SceneUtil::RTTNode Approach

Use OpenMW's RTTNode wrapper (used by water reflections):

### Implementation

```cpp
#include <components/sceneutil/rtt.hpp>

// In SnowDeformationManager
osg::ref_ptr<SceneUtil::RTTNode> mRTTNode;

void SnowDeformationManager::initialize()
{
    createDeformationTexture();

    // Create RTT node
    mRTTNode = new SceneUtil::RTTNode(DEFORMATION_TEXTURE_SIZE, DEFORMATION_TEXTURE_SIZE,
                                       GL_RGBA16F, false, 0);

    // Set up rendering
    osg::ref_ptr<osg::Camera> camera = mRTTNode->getCamera();
    camera->setClearColor(osg::Vec4(0,0,0,0));

    // Add geometry to RTT scene
    camera->addChild(mDecayQuad);
    // Add footprint quads each frame

    mRootNode->addChild(mRTTNode);

    // Get texture from RTT node
    mDeformationTexture = mRTTNode->getColorTexture();
}
```

---

## RECOMMENDATION

**Try solutions in this order:**

1. **SOLUTION 2 (Fixes 1-5)** - Quick fixes to current approach (30 min)
   - If this works, great! If not...

2. **SOLUTION 1 (Manual FBO)** - Proven ripples pattern (2-3 hours)
   - Most likely to work
   - Matches working OpenMW code

3. **SOLUTION 3 (RTTNode)** - If both fail (1-2 hours)
   - Highest level abstraction
   - Might hide underlying issues

## Quick Test for SOLUTION 2

After applying fixes 1-5, test with:

```bash
cd build
cmake --build . --target openmw
./openmw.exe
```

Look for in logs:
- "Shader type ... has source, length: ..."
- No GL errors
- Footprint quads being added

If you see bright red terrain, SUCCESS! If not, move to SOLUTION 1.

## Additional Diagnostics

Add this to terrain.frag to verify texture is updating:

```glsl
#if @snowDeformation
    // Sample texture center pixel
    vec4 centerSample = texture2D(deformationMap, vec2(0.5, 0.5));

    // If center is non-zero, make terrain bright green
    if (centerSample.r > 0.001) {
        gl_FragData[0].xyz = vec3(0.0, 1.0, 0.0);  // BRIGHT GREEN
    }
#endif
```

This will show green terrain if RTT writes anything to texture center.
