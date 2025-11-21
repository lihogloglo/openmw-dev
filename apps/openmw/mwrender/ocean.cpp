#include "ocean.hpp"

#include <components/resource/resourcesystem.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/sceneutil/texturemanager.hpp>

#include <osg/Geometry>
#include <osg/PositionAttitudeTransform>
#include <osg/Texture2DArray>

namespace MWRender
{

    Ocean::Ocean(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mHeight(0.f)
        , mEnabled(false)
        , mTime(0.f)
    {
        mRootNode = new osg::PositionAttitudeTransform;
        
        initShaders();
        initTextures();
        initGeometry();
    }

    Ocean::~Ocean()
    {
        removeFromScene(mParent);
    }

    void Ocean::setEnabled(bool enabled)
    {
        if (mEnabled == enabled)
            return;
        mEnabled = enabled;
        if (mEnabled)
            mParent->addChild(mRootNode);
        else
            mParent->removeChild(mRootNode);
    }

    void Ocean::update(float dt, bool paused)
    {
        if (!mEnabled || paused)
            return;

        mTime += dt;
        updateSimulation(dt);
    }

    void Ocean::setHeight(float height)
    {
        mHeight = height;
        mRootNode->setPosition(osg::Vec3f(0.f, 0.f, mHeight));
    }

    bool Ocean::isUnderwater(const osg::Vec3f& pos) const
    {
        // Simple check for now, ignoring waves
        return pos.z() < mHeight;
    }

    void Ocean::addToScene(osg::Group* parent)
    {
        if (mEnabled && !parent->containsNode(mRootNode))
            parent->addChild(mRootNode);
    }

    void Ocean::removeFromScene(osg::Group* parent)
    {
        if (parent->containsNode(mRootNode))
            parent->removeChild(mRootNode);
    }

    void Ocean::initShaders()
    {
        auto shaderManager = mResourceSystem->getShaderManager();
        
        // Load compute shaders
        // Note: ShaderManager::getShader handles .comp extension
        // We need to create programs for each stage
        
        // Spectrum Compute
        // mComputeSpectrum = shaderManager->getProgram(...); 
        // This part requires more detailed integration with ShaderManager to create Compute Programs
        // For now, placeholder logic
    }

    void Ocean::initTextures()
    {
        // Create textures for FFT
    }

    void Ocean::initGeometry()
    {
        // Create a large grid or clipmap
        mWaterGeom = new osg::Geometry;
        // ... geometry creation ...
        
        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(mWaterGeom);
        mRootNode->addChild(geode);
    }

    void Ocean::updateSimulation(float dt)
    {
        // Dispatch compute shaders
    }

}
