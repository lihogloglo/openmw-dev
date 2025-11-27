#include "water.hpp"

#include <sstream>

#include <osg/ClipNode>
#include <osg/Depth>
#include <osg/Fog>
#include <osg/FrontFace>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Material>
#include <osg/PositionAttitudeTransform>
#include <osg/ViewportIndexed>

#include <osgUtil/CullVisitor>
#include <osgUtil/IncrementalCompileOperation>

#include <components/resource/imagemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/depth.hpp>
#include <components/sceneutil/rtt.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/sceneutil/waterutil.hpp>

#include <components/misc/constants.hpp>
#include <components/stereo/stereomanager.hpp>

#include <components/nifosg/controller.hpp>

#include <components/shader/shadermanager.hpp>

#include <components/esm3/loadcell.hpp>

#include <components/fallback/fallback.hpp>

#include <components/settings/values.hpp>

#include "../mwworld/cellstore.hpp"

#include "renderbin.hpp"
#include "ripples.hpp"
#include "ripplesimulation.hpp"
#include "util.hpp"
#include "vismask.hpp"

namespace MWRender
{

    // --------------------------------------------------------------------------------------------------------------------------------

    /// @brief Allows to cull and clip meshes that are below a plane. Useful for reflection & refraction camera effects.
    /// Also handles flipping of the plane when the eye point goes below it.
    /// To use, simply create the scene as subgraph of this node, then do setPlane(const osg::Plane& plane);
    class ClipCullNode : public osg::Group
    {
        class PlaneCullCallback : public SceneUtil::NodeCallback<PlaneCullCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            /// @param cullPlane The culling plane (in world space).
            PlaneCullCallback(const osg::Plane* cullPlane)
                : mCullPlane(cullPlane)
            {
            }

            void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
            {
                osg::Polytope::PlaneList origPlaneList
                    = cv->getProjectionCullingStack().back().getFrustum().getPlaneList();

                osg::Plane plane = *mCullPlane;
                plane.transform(*cv->getCurrentRenderStage()->getInitialViewMatrix());

                osg::Vec3d eyePoint = cv->getEyePoint();
                if (mCullPlane->intersect(osg::BoundingSphere(osg::Vec3d(0, 0, eyePoint.z()), 0)) > 0)
                    plane.flip();

                cv->getProjectionCullingStack().back().getFrustum().add(plane);

                traverse(node, cv);

                // undo
                cv->getProjectionCullingStack().back().getFrustum().set(origPlaneList);
            }

        private:
            const osg::Plane* mCullPlane;
        };

        class FlipCallback : public SceneUtil::NodeCallback<FlipCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            FlipCallback(const osg::Plane* cullPlane)
                : mCullPlane(cullPlane)
            {
            }

            void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
            {
                osg::Vec3d eyePoint = cv->getEyePoint();

                osg::RefMatrix* modelViewMatrix = new osg::RefMatrix(*cv->getModelViewMatrix());

                // apply the height of the plane
                // we can't apply this height in the addClipPlane() since the "flip the below graph" function would
                // otherwise flip the height as well
                modelViewMatrix->preMultTranslate(mCullPlane->getNormal() * ((*mCullPlane)[3] * -1));

                // flip the below graph if the eye point is above the plane
                if (mCullPlane->intersect(osg::BoundingSphere(osg::Vec3d(0, 0, eyePoint.z()), 0)) > 0)
                {
                    modelViewMatrix->preMultScale(osg::Vec3(1, 1, -1));
                }

                // move the plane back along its normal a little bit to prevent bleeding at the water shore
                float fov = Settings::camera().mFieldOfView;
                const float clipFudgeMin = 2.5; // minimum offset of clip plane
                const float clipFudgeScale = -15000.0;
                float clipFudge = abs(abs((*mCullPlane)[3]) - eyePoint.z()) * fov / clipFudgeScale - clipFudgeMin;
                modelViewMatrix->preMultTranslate(mCullPlane->getNormal() * clipFudge);

                cv->pushModelViewMatrix(modelViewMatrix, osg::Transform::RELATIVE_RF);
                traverse(node, cv);
                cv->popModelViewMatrix();
            }

        private:
            const osg::Plane* mCullPlane;
        };

    public:
        ClipCullNode()
        {
            addCullCallback(new PlaneCullCallback(&mPlane));

            mClipNodeTransform = new osg::Group;
            mClipNodeTransform->addCullCallback(new FlipCallback(&mPlane));
            osg::Group::addChild(mClipNodeTransform);

            mClipNode = new osg::ClipNode;

            mClipNodeTransform->addChild(mClipNode);
        }

        void setPlane(const osg::Plane& plane)
        {
            if (plane == mPlane)
                return;
            mPlane = plane;

            mClipNode->getClipPlaneList().clear();
            mClipNode->addClipPlane(
                new osg::ClipPlane(0, osg::Plane(mPlane.getNormal(), 0))); // mPlane.d() applied in FlipCallback
            mClipNode->setStateSetModes(*getOrCreateStateSet(), osg::StateAttribute::ON);
            mClipNode->setCullingActive(false);
        }

    private:
        osg::ref_ptr<osg::Group> mClipNodeTransform;
        osg::ref_ptr<osg::ClipNode> mClipNode;

        osg::Plane mPlane;
    };

    /// This callback on the Camera has the effect of a RELATIVE_RF_INHERIT_VIEWPOINT transform mode (which does not
    /// exist in OSG). We want to keep the View Point of the parent camera so we will not have to recreate LODs.
    class InheritViewPointCallback
        : public SceneUtil::NodeCallback<InheritViewPointCallback, osg::Node*, osgUtil::CullVisitor*>
    {
    public:
        InheritViewPointCallback() {}

        void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
        {
            osg::ref_ptr<osg::RefMatrix> modelViewMatrix = new osg::RefMatrix(*cv->getModelViewMatrix());
            cv->popModelViewMatrix();
            cv->pushModelViewMatrix(modelViewMatrix, osg::Transform::ABSOLUTE_RF_INHERIT_VIEWPOINT);
            traverse(node, cv);
        }
    };

    /// Moves water mesh away from the camera slightly if the camera gets too close on the Z axis.
    /// The offset works around graphics artifacts that occurred with the GL_DEPTH_CLAMP when the camera gets extremely
    /// close to the mesh (seen on NVIDIA at least). Must be added as a Cull callback.
    class FudgeCallback : public SceneUtil::NodeCallback<FudgeCallback, osg::Node*, osgUtil::CullVisitor*>
    {
    public:
        void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
        {
            const float fudge = 0.2;
            if (std::abs(cv->getEyeLocal().z()) < fudge)
            {
                float diff = fudge - cv->getEyeLocal().z();
                osg::RefMatrix* modelViewMatrix = new osg::RefMatrix(*cv->getModelViewMatrix());

                if (cv->getEyeLocal().z() > 0)
                    modelViewMatrix->preMultTranslate(osg::Vec3f(0, 0, -diff));
                else
                    modelViewMatrix->preMultTranslate(osg::Vec3f(0, 0, diff));

                cv->pushModelViewMatrix(modelViewMatrix, osg::Transform::RELATIVE_RF);
                traverse(node, cv);
                cv->popModelViewMatrix();
            }
            else
                traverse(node, cv);
        }
    };

    class RainSettingsUpdater : public SceneUtil::StateSetUpdater
    {
    public:
        RainSettingsUpdater()
            : mRainIntensity(0.f)
            , mEnableRipples(false)
        {
        }

        void setRainIntensity(float rainIntensity) { mRainIntensity = rainIntensity; }
        void setRipplesEnabled(bool enableRipples) { mEnableRipples = enableRipples; }

    protected:
        void setDefaults(osg::StateSet* stateset) override
        {
            osg::ref_ptr<osg::Uniform> rainIntensityUniform = new osg::Uniform("rainIntensity", 0.0f);
            stateset->addUniform(rainIntensityUniform.get());
            osg::ref_ptr<osg::Uniform> enableRainRipplesUniform = new osg::Uniform("enableRainRipples", false);
            stateset->addUniform(enableRainRipplesUniform.get());
        }

        void apply(osg::StateSet* stateset, osg::NodeVisitor* /*nv*/) override
        {
            osg::ref_ptr<osg::Uniform> rainIntensityUniform = stateset->getUniform("rainIntensity");
            if (rainIntensityUniform != nullptr)
                rainIntensityUniform->set(mRainIntensity);
            osg::ref_ptr<osg::Uniform> enableRainRipplesUniform = stateset->getUniform("enableRainRipples");
            if (enableRainRipplesUniform != nullptr)
                enableRainRipplesUniform->set(mEnableRipples);
        }

    private:
        float mRainIntensity;
        bool mEnableRipples;
    };

    class Refraction : public SceneUtil::RTTNode
    {
    public:
        Refraction(uint32_t rttSize)
            : RTTNode(rttSize, rttSize, 0, false, 1, StereoAwareness::Aware, shouldAddMSAAIntermediateTarget())
            , mNodeMask(Refraction::sDefaultCullMask)
        {
            setDepthBufferInternalFormat(GL_DEPTH24_STENCIL8);
            mClipCullNode = new ClipCullNode;
        }

        void setDefaults(osg::Camera* camera) override
        {
            camera->setReferenceFrame(osg::Camera::RELATIVE_RF);
            camera->setSmallFeatureCullingPixelSize(Settings::water().mSmallFeatureCullingPixelSize);
            camera->setName("RefractionCamera");
            camera->addCullCallback(new InheritViewPointCallback);
            camera->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);

            // No need for fog here, we are already applying fog on the water surface itself as well as underwater fog
            // assign large value to effectively turn off fog
            // shaders don't respect glDisable(GL_FOG)
            osg::ref_ptr<osg::Fog> fog(new osg::Fog);
            fog->setStart(10000000);
            fog->setEnd(10000000);
            camera->getOrCreateStateSet()->setAttributeAndModes(
                fog, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);

            camera->addChild(mClipCullNode);
            camera->setNodeMask(Mask_RenderToTexture);

            if (Settings::water().mRefractionScale != 1) // TODO: to be removed with issue #5709
                SceneUtil::ShadowManager::instance().disableShadowsForStateSet(*camera->getOrCreateStateSet());
        }

        void apply(osg::Camera* camera) override
        {
            camera->setViewMatrix(mViewMatrix);
            camera->setCullMask(mNodeMask);
        }

        void setScene(osg::Node* scene)
        {
            if (mScene)
                mClipCullNode->removeChild(mScene);
            mScene = scene;
            mClipCullNode->addChild(scene);
        }

        void setWaterLevel(float waterLevel)
        {
            const float refractionScale = Settings::water().mRefractionScale;

            mViewMatrix = osg::Matrix::scale(1, 1, refractionScale)
                * osg::Matrix::translate(0, 0, (1.0 - refractionScale) * waterLevel);

            mClipCullNode->setPlane(osg::Plane(osg::Vec3d(0, 0, -1), osg::Vec3d(0, 0, waterLevel)));
        }

        void showWorld(bool show)
        {
            if (show)
                mNodeMask = Refraction::sDefaultCullMask;
            else
                mNodeMask = Refraction::sDefaultCullMask & ~sToggleWorldMask;
        }

    private:
        osg::ref_ptr<ClipCullNode> mClipCullNode;
        osg::ref_ptr<osg::Node> mScene;
        osg::Matrix mViewMatrix{ osg::Matrix::identity() };

        unsigned int mNodeMask;

        static constexpr unsigned int sDefaultCullMask = Mask_Effect | Mask_Scene | Mask_Object | Mask_Static
            | Mask_Terrain | Mask_Actor | Mask_ParticleSystem | Mask_Sky | Mask_Sun | Mask_Player | Mask_Lighting
            | Mask_Groundcover;
    };

    class Reflection : public SceneUtil::RTTNode
    {
    public:
        Reflection(uint32_t rttSize, bool isInterior)
            : RTTNode(rttSize, rttSize, 0, false, 0, StereoAwareness::Aware, shouldAddMSAAIntermediateTarget())
        {
            setInterior(isInterior);
            setDepthBufferInternalFormat(GL_DEPTH24_STENCIL8);
            mClipCullNode = new ClipCullNode;
        }

        void setDefaults(osg::Camera* camera) override
        {
            camera->setReferenceFrame(osg::Camera::RELATIVE_RF);
            camera->setSmallFeatureCullingPixelSize(Settings::water().mSmallFeatureCullingPixelSize);
            camera->setName("ReflectionCamera");
            camera->addCullCallback(new InheritViewPointCallback);

            // Inform the shader that we're in a reflection
            camera->getOrCreateStateSet()->addUniform(new osg::Uniform("isReflection", true));

            // XXX: should really flip the FrontFace on each renderable instead of forcing clockwise.
            osg::ref_ptr<osg::FrontFace> frontFace(new osg::FrontFace);
            frontFace->setMode(osg::FrontFace::CLOCKWISE);
            camera->getOrCreateStateSet()->setAttributeAndModes(frontFace, osg::StateAttribute::ON);

            camera->addChild(mClipCullNode);
            camera->setNodeMask(Mask_RenderToTexture);

            SceneUtil::ShadowManager::instance().disableShadowsForStateSet(*camera->getOrCreateStateSet());
        }

        void apply(osg::Camera* camera) override
        {
            camera->setViewMatrix(mViewMatrix);
            camera->setCullMask(mNodeMask);
        }

        void setInterior(bool isInterior)
        {
            mInterior = isInterior;
            mNodeMask = calcNodeMask();
        }

        void setWaterLevel(float waterLevel)
        {
            mViewMatrix = osg::Matrix::scale(1, 1, -1) * osg::Matrix::translate(0, 0, 2 * waterLevel);
            mClipCullNode->setPlane(osg::Plane(osg::Vec3d(0, 0, 1), osg::Vec3d(0, 0, waterLevel)));
        }

        void setScene(osg::Node* scene)
        {
            if (mScene)
                mClipCullNode->removeChild(mScene);
            mScene = scene;
            mClipCullNode->addChild(scene);
        }

        void showWorld(bool show)
        {
            if (show)
                mNodeMask = calcNodeMask();
            else
                mNodeMask = calcNodeMask() & ~sToggleWorldMask;
        }

    private:
        unsigned int calcNodeMask()
        {
            int reflectionDetail = Settings::water().mReflectionDetail;
            reflectionDetail = std::clamp(reflectionDetail, mInterior ? 2 : 0, 5);
            unsigned int extraMask = 0;
            if (reflectionDetail >= 1)
                extraMask |= Mask_Terrain;
            if (reflectionDetail >= 2)
                extraMask |= Mask_Static;
            if (reflectionDetail >= 3)
                extraMask |= Mask_Effect | Mask_ParticleSystem | Mask_Object;
            if (reflectionDetail >= 4)
                extraMask |= Mask_Player | Mask_Actor;
            if (reflectionDetail >= 5)
                extraMask |= Mask_Groundcover;
            return Mask_Scene | Mask_Sky | Mask_Lighting | extraMask;
        }

        osg::ref_ptr<ClipCullNode> mClipCullNode;
        osg::ref_ptr<osg::Node> mScene;
        osg::Node::NodeMask mNodeMask;
        osg::Matrix mViewMatrix{ osg::Matrix::identity() };
        bool mInterior;
    };

    /// DepthClampCallback enables GL_DEPTH_CLAMP for the current draw, if supported.
    class DepthClampCallback : public osg::Drawable::DrawCallback
    {
    public:
        void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* drawable) const override
        {
            static bool supported = osg::isGLExtensionOrVersionSupported(
                renderInfo.getState()->getContextID(), "GL_ARB_depth_clamp", 3.3);
            if (!supported)
            {
                drawable->drawImplementation(renderInfo);
                return;
            }

            glEnable(GL_DEPTH_CLAMP);

            drawable->drawImplementation(renderInfo);

            // restore default
            glDisable(GL_DEPTH_CLAMP);
        }
    };

    WaterManager::WaterManager(osg::Group* parent, osg::Group* sceneRoot, Resource::ResourceSystem* resourceSystem,
        osgUtil::IncrementalCompileOperation* ico)
        : mRainSettingsUpdater(nullptr)
        , mParent(parent)
        , mSceneRoot(sceneRoot)
        , mResourceSystem(resourceSystem)
        , mEnabled(true)
        , mToggled(true)
        , mTop(0)
        , mInterior(false)
        , mShowWorld(true)
        , mCullCallback(nullptr)
        , mShaderWaterStateSetUpdater(nullptr)
        , mUseOcean(false)
    {
        mOcean = std::make_unique<Ocean>(mParent, mResourceSystem);
        mLake = std::make_unique<Lake>(mParent, mResourceSystem);
        // Ocean enabled with smart masking using WaterHeightField
        mUseOcean = true;

        // Initialize water height field for multi-altitude water support
        mWaterHeightField = std::make_unique<WaterHeightField>(2048, 0.1f);

        // Initialize cubemap reflection system for lakes/rivers (SSR is inline in shader)
        mCubemapManager = std::make_unique<CubemapReflectionManager>(mParent, mSceneRoot, mResourceSystem);
        
        // Increase max regions to support multiple lakes
        CubemapReflectionManager::Params params;
        params.maxRegions = 32; // Support up to 32 active lake regions
        params.resolution = 512;
        params.updateInterval = 2.0f; // Update every 2 seconds
        mCubemapManager->setParams(params);
        
        mCubemapManager->initialize();

        // Connect lake to WaterManager for reflection system access
        if (mLake)
            mLake->setWaterManager(this);

        mSimulation = std::make_unique<RippleSimulation>(mSceneRoot, resourceSystem);

        mWaterGeom = SceneUtil::createWaterGeometry(Constants::CellSizeInUnits * 150, 40, 900);
        mWaterGeom->setDrawCallback(new DepthClampCallback);
        mWaterGeom->setNodeMask(Mask_Water);
        mWaterGeom->setDataVariance(osg::Object::STATIC);
        mWaterGeom->setName("Water Geometry");

        mWaterNode = new osg::PositionAttitudeTransform;
        mWaterNode->setName("Water Root");
        mWaterNode->addChild(mWaterGeom);
        mWaterNode->addCullCallback(new FudgeCallback);

        // simple water fallback for the local map
        osg::ref_ptr<osg::Geometry> geom2(osg::clone(mWaterGeom.get(), osg::CopyOp::DEEP_COPY_NODES));
        createSimpleWaterStateSet(geom2, Fallback::Map::getFloat("Water_Map_Alpha"));
        geom2->setNodeMask(Mask_SimpleWater);
        geom2->setName("Simple Water Geometry");
        mWaterNode->addChild(geom2);

        mSceneRoot->addChild(mWaterNode);

        setHeight(mTop);

        updateWaterMaterial();

        // Load test lakes for multi-altitude water
        loadLakesFromJSON("");

        if (ico)
            ico->add(mWaterNode);
    }

    void WaterManager::setCullCallback(osg::Callback* callback)
    {
        if (mCullCallback)
        {
            mWaterNode->removeCullCallback(mCullCallback);
            if (mReflection)
                mReflection->removeCullCallback(mCullCallback);
            if (mRefraction)
                mRefraction->removeCullCallback(mCullCallback);
        }

        mCullCallback = callback;

        if (callback)
        {
            mWaterNode->addCullCallback(callback);
            if (mReflection)
                mReflection->addCullCallback(callback);
            if (mRefraction)
                mRefraction->addCullCallback(callback);
        }
    }

    void WaterManager::updateWaterMaterial()
    {
        if (mShaderWaterStateSetUpdater)
        {
            mWaterNode->removeCullCallback(mShaderWaterStateSetUpdater);
            mShaderWaterStateSetUpdater = nullptr;
        }
        if (mReflection)
        {
            mParent->removeChild(mReflection);
            mReflection = nullptr;
        }
        if (mRefraction)
        {
            mParent->removeChild(mRefraction);
            mRefraction = nullptr;
        }
        if (mRipples)
        {
            mParent->removeChild(mRipples);
            mRipples = nullptr;
            mSimulation->setRipples(nullptr);
        }

        mWaterNode->setStateSet(nullptr);
        mWaterGeom->setStateSet(nullptr);
        mWaterGeom->setUpdateCallback(nullptr);

        if (Settings::water().mShader)
        {
            const unsigned int rttSize = Settings::water().mRttSize;

            mReflection = new Reflection(rttSize, mInterior);
            mReflection->setWaterLevel(mTop);
            mReflection->setScene(mSceneRoot);
            if (mCullCallback)
                mReflection->addCullCallback(mCullCallback);
            mParent->addChild(mReflection);

            if (Settings::water().mRefraction)
            {
                mRefraction = new Refraction(rttSize);
                mRefraction->setWaterLevel(mTop);
                mRefraction->setScene(mSceneRoot);
                if (mCullCallback)
                    mRefraction->addCullCallback(mCullCallback);
                mParent->addChild(mRefraction);
            }

            mRipples = new Ripples(mResourceSystem);
            mSimulation->setRipples(mRipples);
            mParent->addChild(mRipples);

            showWorld(mShowWorld);

            createShaderWaterStateSet(mWaterNode);
        }
        else
            createSimpleWaterStateSet(mWaterGeom, Fallback::Map::getFloat("Water_World_Alpha"));

        mResourceSystem->getSceneManager()->setUpNormalsRTForStateSet(mWaterGeom->getOrCreateStateSet(), true);

        updateVisible();
    }

    osg::Vec3d WaterManager::getPosition() const
    {
        return mWaterNode->getPosition();
    }

    void WaterManager::createSimpleWaterStateSet(osg::Node* node, float alpha)
    {
        osg::ref_ptr<osg::StateSet> stateset = SceneUtil::createSimpleWaterStateSet(alpha, MWRender::RenderBin_Water);

        node->setStateSet(stateset);
        node->setUpdateCallback(nullptr);
        mRainSettingsUpdater = nullptr;

        // Add animated textures
        std::vector<osg::ref_ptr<osg::Texture2D>> textures;
        const int frameCount = std::clamp(Fallback::Map::getInt("Water_SurfaceFrameCount"), 0, 320);
        std::string_view texture = Fallback::Map::getString("Water_SurfaceTexture");
        for (int i = 0; i < frameCount; ++i)
        {
            std::ostringstream texname;
            texname << "textures/water/" << texture << std::setw(2) << std::setfill('0') << i << ".dds";
            const VFS::Path::Normalized path(texname.str());
            osg::ref_ptr<osg::Texture2D> tex(new osg::Texture2D(mResourceSystem->getImageManager()->getImage(path)));
            tex->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
            tex->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
            mResourceSystem->getSceneManager()->applyFilterSettings(tex);
            textures.push_back(tex);
        }

        if (textures.empty())
            return;

        float fps = Fallback::Map::getFloat("Water_SurfaceFPS");

        osg::ref_ptr<NifOsg::FlipController> controller(new NifOsg::FlipController(0, 1.f / fps, textures));
        controller->setSource(std::make_shared<SceneUtil::FrameTimeSource>());
        node->setUpdateCallback(controller);

        stateset->setTextureAttributeAndModes(0, textures[0], osg::StateAttribute::ON);

        // use a shader to render the simple water, ensuring that fog is applied per pixel as required.
        // this could be removed if a more detailed water mesh, using some sort of paging solution, is implemented.
        Resource::SceneManager* sceneManager = mResourceSystem->getSceneManager();
        bool oldValue = sceneManager->getForceShaders();
        sceneManager->setForceShaders(true);
        sceneManager->recreateShaders(node);
        sceneManager->setForceShaders(oldValue);
    }

    class ShaderWaterStateSetUpdater : public SceneUtil::StateSetUpdater
    {
    public:
        ShaderWaterStateSetUpdater(WaterManager* water, Reflection* reflection, Refraction* refraction, Ripples* ripples,
            osg::ref_ptr<osg::Program> program, osg::ref_ptr<osg::Texture2D> normalMap)
            : mWater(water)
            , mReflection(reflection)
            , mRefraction(refraction)
            , mRipples(ripples)
            , mProgram(std::move(program))
            , mNormalMap(std::move(normalMap))
        {
        }

        void setDefaults(osg::StateSet* stateset) override
        {
            stateset->addUniform(new osg::Uniform("normalMap", 0));
            stateset->setTextureAttributeAndModes(0, mNormalMap, osg::StateAttribute::ON);
            stateset->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
            stateset->setAttributeAndModes(mProgram, osg::StateAttribute::ON);

            stateset->addUniform(new osg::Uniform("reflectionMap", 1));
            if (mRefraction)
            {
                stateset->addUniform(new osg::Uniform("refractionMap", 2));
                stateset->addUniform(new osg::Uniform("refractionDepthMap", 3));
                stateset->setRenderBinDetails(MWRender::RenderBin_Default, "RenderBin");
            }
            else
            {
                stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
                stateset->setRenderBinDetails(MWRender::RenderBin_Water, "RenderBin");
                osg::ref_ptr<osg::Depth> depth = new SceneUtil::AutoDepth;
                depth->setWriteMask(false);
                stateset->setAttributeAndModes(depth, osg::StateAttribute::ON);
            }
            if (mRipples)
            {
                stateset->addUniform(new osg::Uniform("rippleMap", 4));
            }

            stateset->addUniform(new osg::Uniform("nodePosition", osg::Vec3f(mWater->getPosition())));
        }

        void apply(osg::StateSet* stateset, osg::NodeVisitor* nv) override
        {
            osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(nv);
            stateset->setTextureAttributeAndModes(1, mReflection->getColorTexture(cv), osg::StateAttribute::ON);

            if (mRefraction)
            {
                stateset->setTextureAttributeAndModes(2, mRefraction->getColorTexture(cv), osg::StateAttribute::ON);
                stateset->setTextureAttributeAndModes(3, mRefraction->getDepthTexture(cv), osg::StateAttribute::ON);
            }
            if (mRipples)
            {
                stateset->setTextureAttributeAndModes(4, mRipples->getColorTexture(), osg::StateAttribute::ON);
            }

            stateset->getUniform("nodePosition")->set(osg::Vec3f(mWater->getPosition()));
        }

    private:
        WaterManager* mWater;
        Reflection* mReflection;
        Refraction* mRefraction;
        Ripples* mRipples;
        osg::ref_ptr<osg::Program> mProgram;
        osg::ref_ptr<osg::Texture2D> mNormalMap;
    };

    void WaterManager::createShaderWaterStateSet(osg::Node* node)
    {
        // use a define map to conditionally compile the shader
        std::map<std::string, std::string> defineMap;
        defineMap["waterRefraction"] = std::string(mRefraction ? "1" : "0");
        const int rippleDetail = Settings::water().mRainRippleDetail;
        defineMap["rainRippleDetail"] = std::to_string(rippleDetail);
        defineMap["rippleMapWorldScale"] = std::to_string(RipplesSurface::sWorldScaleFactor);
        defineMap["rippleMapSize"] = std::to_string(RipplesSurface::sRTTSize) + ".0";
        defineMap["sunlightScattering"] = Settings::water().mSunlightScattering ? "1" : "0";
        defineMap["wobblyShores"] = Settings::water().mWobblyShores ? "1" : "0";

        Stereo::shaderStereoDefines(defineMap);

        Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();
        osg::ref_ptr<osg::Program> program = shaderMgr.getProgram("water", defineMap);

        constexpr VFS::Path::NormalizedView waterImage("textures/omw/water_nm.png");
        osg::ref_ptr<osg::Texture2D> normalMap(
            new osg::Texture2D(mResourceSystem->getImageManager()->getImage(waterImage)));
        normalMap->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        normalMap->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
        mResourceSystem->getSceneManager()->applyFilterSettings(normalMap);

        mRainSettingsUpdater = new RainSettingsUpdater();
        node->setUpdateCallback(mRainSettingsUpdater);

        mShaderWaterStateSetUpdater = new ShaderWaterStateSetUpdater(
            this, mReflection, mRefraction, mRipples, std::move(program), std::move(normalMap));
        node->addCullCallback(mShaderWaterStateSetUpdater);
    }

    void WaterManager::processChangedSettings(const Settings::CategorySettingVector& settings)
    {
        updateWaterMaterial();
    }

    WaterManager::~WaterManager()
    {
        mParent->removeChild(mWaterNode);

        if (mReflection)
        {
            mParent->removeChild(mReflection);
            mReflection = nullptr;
        }
        if (mRefraction)
        {
            mParent->removeChild(mRefraction);
            mRefraction = nullptr;
        }
        if (mRipples)
        {
            mParent->removeChild(mRipples);
            mRipples = nullptr;
            mSimulation->setRipples(nullptr);
        }
    }

    void WaterManager::listAssetsToPreload(std::vector<VFS::Path::Normalized>& textures)
    {
        const int frameCount = std::clamp(Fallback::Map::getInt("Water_SurfaceFrameCount"), 0, 320);
        std::string_view texture = Fallback::Map::getString("Water_SurfaceTexture");
        for (int i = 0; i < frameCount; ++i)
        {
            std::ostringstream texname;
            texname << "textures/water/" << texture << std::setw(2) << std::setfill('0') << i << ".dds";
            textures.emplace_back(texname.str());
        }
    }

    void WaterManager::setEnabled(bool enabled)
    {
        mEnabled = enabled;

        // Simple fallback classification (will be overridden by update() using WaterHeightField)
        // For now, just disable both to avoid the blue square
        // The update() function will enable the correct one based on camera position
        if (mUseOcean && mOcean)
            mOcean->setEnabled(false);

        if (mLake)
            mLake->setEnabled(false);

        updateVisible();
    }

    void WaterManager::changeCell(const MWWorld::CellStore* store)
    {
        bool isInterior = !store->getCell()->isExterior();
        bool wasInterior = mInterior;
        if (!isInterior)
        {
            // Exterior
            mWaterNode->setPosition(
                getSceneNodeCoordinates(store->getCell()->getGridX(), store->getCell()->getGridY()));
            mInterior = false;

            // Ocean and Lake will be enabled/disabled based on water type in update()

            // Ocean and Lake will be enabled/disabled based on water type in update()
        }
        else
        {
            mWaterNode->setPosition(osg::Vec3f(0, 0, mTop));
            mInterior = true;

            // Interior: Also disable Lake, use old water
            if (mLake)
                mLake->setEnabled(false);
            if (mUseOcean && mOcean)
                mOcean->setEnabled(false);

            // Create cubemap region for interior water
            if (mCubemapManager)
            {
                osg::Vec3f cubemapCenter(0, 0, mTop);

                // Check if we need to add a region (limit to 8 max)
                if (mCubemapManager->getRegionCount() < 8)
                {
                    // Add cubemap with 500 unit radius for interiors (smaller spaces)
                    mCubemapManager->addRegion(cubemapCenter, 500.0f);
                }
            }
        }
        if (mInterior != wasInterior && mReflection)
            mReflection->setInterior(mInterior);
    }

    void WaterManager::setHeight(const float height)
    {
        mTop = height;

        // Just set heights, don't enable/disable here
        // The update() function will enable the correct water type based on camera position
        if (mUseOcean && mOcean)
            mOcean->setHeight(height);

        if (mLake)
            mLake->setHeight(height);

        mSimulation->setWaterHeight(height);

        osg::Vec3f pos = mWaterNode->getPosition();
        pos.z() = height;
        mWaterNode->setPosition(pos);

        if (mReflection)
            mReflection->setWaterLevel(mTop);
        if (mRefraction)
            mRefraction->setWaterLevel(mTop);

        updateVisible();
    }

    void WaterManager::setRainIntensity(float rainIntensity)
    {
        if (mRainSettingsUpdater)
            mRainSettingsUpdater->setRainIntensity(rainIntensity);
    }

    void WaterManager::setRainRipplesEnabled(bool enableRipples)
    {
        if (mRainSettingsUpdater)
            mRainSettingsUpdater->setRipplesEnabled(enableRipples);
    }

    void WaterManager::update(float dt, bool paused, const osg::Vec3f& cameraPos)
    {
        // Determine water type at camera position using WaterHeightField
        WaterType currentWaterType = WaterType::None;
        float waterHeight = mTop;

        if (mWaterHeightField)
        {
            currentWaterType = mWaterHeightField->sampleType(cameraPos);
            float sampledHeight = mWaterHeightField->sampleHeight(cameraPos);
            if (sampledHeight > -999.0f)  // Valid height
                waterHeight = sampledHeight;
        }

        // Fallback to simple check if height field unavailable
        if (currentWaterType == WaterType::None && !mInterior)
        {
            currentWaterType = (std::abs(mTop) <= 10.0f) ? WaterType::Ocean : WaterType::Lake;
        }

        // TEMPORARY FIX: Check if Lake system has water at current position
        // This handles programmatically-added lakes that the WaterHeightField doesn't know about
        if (mLake && !mInterior)
        {
            float lakeHeight = mLake->getWaterHeightAt(cameraPos);
            if (lakeHeight > -999.0f)  // Lake system has water here
            {
                currentWaterType = WaterType::Lake;
                waterHeight = lakeHeight;
            }
        }

        bool useOcean = (currentWaterType == WaterType::Ocean);
        bool useLake = (currentWaterType == WaterType::Lake || currentWaterType == WaterType::River);

        // Enable/disable water bodies based on type
        if (mOcean)
            mOcean->setEnabled(mEnabled && mUseOcean && useOcean);
        if (mLake)
            mLake->setEnabled(mEnabled && useLake);

        // Update the active water body
        if (mUseOcean && mOcean && mEnabled && useOcean)
            mOcean->update(dt, paused, cameraPos);

        if (mLake && mEnabled && useLake)
            mLake->update(dt, paused, cameraPos);

        // Update cubemap reflections for lakes/rivers
        if (useLake && mEnabled && mCubemapManager)
            mCubemapManager->update(dt, cameraPos);

        if (!paused)
        {
            mSimulation->update(dt);
        }

        if (mRipples)
        {
            mRipples->setPaused(paused);
        }
    }

    void WaterManager::updateVisible()
    {
        bool visible = mEnabled && mToggled;

        // Use new water system (Ocean/Lake with SSR)
        bool useNewWater = true; // Use Ocean/Lake based on water type
        mWaterNode->setNodeMask((visible && !useNewWater) ? ~0u : 0u);

        if (mRefraction)
            mRefraction->setNodeMask(visible ? Mask_RenderToTexture : 0u);
        if (mReflection)
            mReflection->setNodeMask(visible ? Mask_RenderToTexture : 0u);
        if (mRipples)
            mRipples->setNodeMask(visible ? Mask_RenderToTexture : 0u);
    }

    bool WaterManager::toggle()
    {
        mToggled = !mToggled;
        updateVisible();
        return mToggled;
    }

    bool WaterManager::isUnderwater(const osg::Vec3f& pos) const
    {
        return pos.z() < mTop && mToggled && mEnabled;
    }

    osg::Vec3f WaterManager::getSceneNodeCoordinates(int gridX, int gridY)
    {
        return osg::Vec3f(static_cast<float>(gridX * Constants::CellSizeInUnits + (Constants::CellSizeInUnits / 2)),
            static_cast<float>(gridY * Constants::CellSizeInUnits + (Constants::CellSizeInUnits / 2)), mTop);
    }

    void WaterManager::addEmitter(const MWWorld::Ptr& ptr, float scale, float force)
    {
        mSimulation->addEmitter(ptr, scale, force);
    }

    void WaterManager::removeEmitter(const MWWorld::Ptr& ptr)
    {
        mSimulation->removeEmitter(ptr);
    }

    void WaterManager::updateEmitterPtr(const MWWorld::Ptr& old, const MWWorld::Ptr& ptr)
    {
        mSimulation->updateEmitterPtr(old, ptr);
    }

    void WaterManager::emitRipple(const osg::Vec3f& pos)
    {
        mSimulation->emitRipple(pos);
    }

    void WaterManager::addCell(const MWWorld::CellStore* store)
    {
        // Track loaded cells for height field updates
        auto it = std::find(mLoadedCells.begin(), mLoadedCells.end(), store);
        if (it == mLoadedCells.end())
        {
            mLoadedCells.push_back(store);
            updateWaterHeightField();

            // Show lake water for this cell if it exists
            if (mLake && store && store->getCell()->isExterior())
            {
                int gridX = store->getCell()->getGridX();
                int gridY = store->getCell()->getGridY();
                mLake->showWaterCell(gridX, gridY);
            }
        }
    }

    void WaterManager::removeCell(const MWWorld::CellStore* store)
    {
        mSimulation->removeCell(store);

        // Hide lake water for this cell if it exists
        if (mLake && store && store->getCell()->isExterior())
        {
            int gridX = store->getCell()->getGridX();
            int gridY = store->getCell()->getGridY();
            mLake->hideWaterCell(gridX, gridY);
        }

        // Remove from loaded cells tracking
        auto it = std::find(mLoadedCells.begin(), mLoadedCells.end(), store);
        if (it != mLoadedCells.end())
        {
            mLoadedCells.erase(it);
            updateWaterHeightField();
        }
    }

    void WaterManager::updateWaterHeightField()
    {
        if (mWaterHeightField)
        {
            mWaterHeightField->updateFromLoadedCells(mLoadedCells);

            // Generate and update ocean mask to prevent ocean rendering in inland areas
            if (mOcean && mUseOcean)
            {
                osg::Image* oceanMask = mWaterHeightField->generateOceanMask();
                mOcean->setOceanMask(oceanMask, mWaterHeightField->getOrigin(),
                    mWaterHeightField->getTexelsPerUnit());
            }
        }
    }

    void WaterManager::clearRipples()
    {
        mSimulation->clear();
    }

    void WaterManager::showWorld(bool show)
    {
        if (mReflection)
            mReflection->showWorld(show);
        if (mRefraction)
            mRefraction->showWorld(show);
        mShowWorld = show;
    }

    // Ocean parameter accessors for console commands
    void WaterManager::setOceanWaterColor(const osg::Vec3f& color)
    {
        if (mOcean)
            mOcean->setWaterColor(color);
    }

    void WaterManager::setOceanFoamColor(const osg::Vec3f& color)
    {
        if (mOcean)
            mOcean->setFoamColor(color);
    }

    void WaterManager::setOceanWindSpeed(float speed)
    {
        if (mOcean)
            mOcean->setWindSpeed(speed);
    }

    void WaterManager::setOceanWindDirection(float degrees)
    {
        if (mOcean)
            mOcean->setWindDirection(degrees);
    }

    void WaterManager::setOceanFetchLength(float length)
    {
        if (mOcean)
            mOcean->setFetchLength(length);
    }

    void WaterManager::setOceanSwell(float swell)
    {
        if (mOcean)
            mOcean->setSwell(swell);
    }

    void WaterManager::setOceanDetail(float detail)
    {
        if (mOcean)
            mOcean->setDetail(detail);
    }

    void WaterManager::setOceanSpread(float spread)
    {
        if (mOcean)
            mOcean->setSpread(spread);
    }

    void WaterManager::setOceanFoamAmount(float amount)
    {
        if (mOcean)
            mOcean->setFoamAmount(amount);
    }

    osg::Vec3f WaterManager::getOceanWaterColor() const
    {
        return mOcean ? mOcean->getWaterColor() : osg::Vec3f(0.15f, 0.25f, 0.35f);
    }

    osg::Vec3f WaterManager::getOceanFoamColor() const
    {
        return mOcean ? mOcean->getFoamColor() : osg::Vec3f(1.0f, 1.0f, 1.0f);
    }

    float WaterManager::getOceanWindSpeed() const
    {
        return mOcean ? mOcean->getWindSpeed() : 20.0f;
    }

    float WaterManager::getOceanWindDirection() const
    {
        return mOcean ? mOcean->getWindDirection() : 0.0f;
    }

    float WaterManager::getOceanFetchLength() const
    {
        return mOcean ? mOcean->getFetchLength() : 550000.0f;
    }

    float WaterManager::getOceanSwell() const
    {
        return mOcean ? mOcean->getSwell() : 0.8f;
    }

    float WaterManager::getOceanDetail() const
    {
        return mOcean ? mOcean->getDetail() : 1.0f;
    }

    float WaterManager::getOceanSpread() const
    {
        return mOcean ? mOcean->getSpread() : 0.2f;
    }

    float WaterManager::getOceanFoamAmount() const
    {
        return mOcean ? mOcean->getFoamAmount() : 5.0f;
    }

    osg::TextureCubeMap* WaterManager::getCubemapForPosition(const osg::Vec3f& pos)
    {
        if (mCubemapManager)
            return mCubemapManager->getCubemapForPosition(pos);
        return nullptr;
    }

    void WaterManager::addLakeCell(int gridX, int gridY, float height)
    {
        if (mLake)
        {
            mLake->addWaterCell(gridX, gridY, height);

            // Add a cubemap region for this lake cell
            if (mCubemapManager)
            {
                // Calculate world position for cubemap center
                // Place it slightly above the water surface (e.g., player eye height ~1.7m = ~110 units)
                // But for reflections, surface level is usually best, or slightly above to avoid clipping waves
                float worldX, worldY;
                Units::gridToWorld(gridX, gridY, worldX, worldY);
                osg::Vec3f center(worldX, worldY, height + 64.0f); // ~3 feet above water

                // Use cell size as radius (8192 units)
                // This ensures the cubemap covers the whole cell
                mCubemapManager->addRegion(center, Constants::CellSizeInUnits);
            }
        }
    }

    void WaterManager::addLakeAtWorldPos(float worldX, float worldY, float height)
    {
        // Convert world coordinates to grid cell
        const float cellSize = Constants::CellSizeInUnits;  // 8192 MW units
        int gridX = static_cast<int>(std::floor(worldX / cellSize));
        int gridY = static_cast<int>(std::floor(worldY / cellSize));

        addLakeCell(gridX, gridY, height);
    }

    void WaterManager::removeLakeCell(int gridX, int gridY)
    {
        if (mLake)
            mLake->removeWaterCell(gridX, gridY);
    }

    void WaterManager::removeLakeAtWorldPos(float worldX, float worldY)
    {
        // Convert world coordinates to grid cell
        const float cellSize = Constants::CellSizeInUnits;  // 8192 MW units
        int gridX = static_cast<int>(std::floor(worldX / cellSize));
        int gridY = static_cast<int>(std::floor(worldY / cellSize));

        removeLakeCell(gridX, gridY);
    }

    void WaterManager::loadLakesFromJSON(const std::string& filepath)
    {
        // TODO: Implement JSON parsing to load lake data from file
        // Expected JSON format:
        // {
        //   "lakes": [
        //     { "worldX": 20803.70, "worldY": -61583.41, "height": 498.96, "waterColor": [0.15, 0.25, 0.35] },
        //     ...
        //   ]
        // }

        // ============================================================================
        // TEMPORARY: Hardcoded test lakes for rendering validation
        // TODO: Remove this section once .omwaddon integration is complete
        // ============================================================================

        Log(Debug::Info) << "[Lake] Loading temporary hardcoded test lakes...";

        // If filepath provided, try to load JSON (not yet implemented)
        if (!filepath.empty())
        {
            Log(Debug::Warning) << "loadLakesFromJSON not yet implemented - filepath: " << filepath;
            // JSON parsing implementation would go here
        }

        // Test lakes at different altitudes using actual Morrowind world coordinates
        // Note: 22.1 units = 1 foot, so ~1450 units = 20 meters

        // Player Position Test - Lake at test location cell (2, -8)
        addLakeAtWorldPos(20803.70f, -61583.41f, 500.0f);  // ~22.6 feet / 6.9m
        Log(Debug::Info) << "[Lake] Added test lake at player position: (20803.70, -61583.41) height 500 (~7m)";

        // HIGH ALTITUDE TEST LAKES around cell (2, -8) for altitude testing
        // These are intentionally at extreme altitudes with dramatic differences to test multi-level water rendering

        addLakeAtWorldPos(28000.0f, -62000.0f, 2200.0f);  // Cell (3, -8) - ~99 feet / 30m
        Log(Debug::Info) << "[Lake] Added high-altitude test lake at cell (3, -8) height 2200 (~30m)";

        addLakeAtWorldPos(20000.0f, -53000.0f, 4400.0f);  // Cell (2, -7) - ~199 feet / 61m
        Log(Debug::Info) << "[Lake] Added high-altitude test lake at cell (2, -7) height 4400 (~61m)";

        addLakeAtWorldPos(12000.0f, -62000.0f, 6600.0f);  // Cell (1, -8) - ~299 feet / 91m
        Log(Debug::Info) << "[Lake] Added high-altitude test lake at cell (1, -8) height 6600 (~91m)";

        addLakeAtWorldPos(26000.0f, -54000.0f, 8800.0f);  // Cell (3, -7) - ~398 feet / 121m
        Log(Debug::Info) << "[Lake] Added high-altitude test lake at cell (3, -7) height 8800 (~121m)";

        addLakeAtWorldPos(10000.0f, -52000.0f, 11000.0f);  // Cell (1, -7) - ~498 feet / 152m
        Log(Debug::Info) << "[Lake] Added high-altitude test lake at cell (1, -7) height 11000 (~152m)";

        // Real Morrowind location test lakes at reasonable altitudes

        addLakeAtWorldPos(2380.0f, -56032.0f, 0.0f);     // Pelagiad area (sea level)
        Log(Debug::Info) << "[Lake] Added test lake near Pelagiad at sea level";

        addLakeAtWorldPos(-22528.0f, -15360.0f, 1500.0f);  // Balmora/Odai River - ~68 feet / 21m
        Log(Debug::Info) << "[Lake] Added test lake at Balmora/Odai River height 1500 (~21m)";

        addLakeAtWorldPos(-11264.0f, 34816.0f, 5800.0f);  // Caldera - ~262 feet / 80m
        Log(Debug::Info) << "[Lake] Added test lake at Caldera height 5800 (~80m)";

        addLakeAtWorldPos(19072.0f, -71680.0f, 0.0f);    // Vivec (sea level)
        Log(Debug::Info) << "[Lake] Added test lake at Vivec at sea level";

        addLakeAtWorldPos(40960.0f, 81920.0f, 15000.0f);  // Red Mountain - ~679 feet / 207m
        Log(Debug::Info) << "[Lake] Added test lake at Red Mountain height 15000 (~207m)";

        Log(Debug::Info) << "[Lake] Finished loading " << (mLake ? mLake->getCellCount() : 0) << " temporary test lakes";

        // ============================================================================
        // END TEMPORARY SECTION
        // ============================================================================
    }

    void WaterManager::setLakeDebugMode(int mode)
    {
        if (mLake)
            mLake->setDebugMode(mode);
    }

    void WaterManager::setSceneBuffers(osg::Texture2D* colorBuffer, osg::Texture2D* depthBuffer)
    {
        mSceneColorBuffer = colorBuffer;
        mSceneDepthBuffer = depthBuffer;
    }

    osg::Texture2D* WaterManager::getSceneColorBuffer()
    {
        return mSceneColorBuffer.get();
    }

    osg::Texture2D* WaterManager::getSceneDepthBuffer()
    {
        return mSceneDepthBuffer.get();
    }

}
