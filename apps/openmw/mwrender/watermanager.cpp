#include "watermanager.hpp"

#include "water.hpp"

#include <components/debug/debuglog.hpp>

#include "../mwworld/cellstore.hpp"

namespace MWRender
{
    WaterManager::WaterManager(osg::Group* parent, osg::Group* sceneRoot, Resource::ResourceSystem* resourceSystem,
        osgUtil::IncrementalCompileOperation* ico)
        : mParent(parent)
        , mSceneRoot(sceneRoot)
        , mResourceSystem(resourceSystem)
        , mIncrementalCompileOperation(ico)
        , mFFTOceanEnabled(true)  // Enable by default (can be configured via settings)
    {
        // For now, create the legacy water renderer
        // In the future, we'll create different renderers based on water type
        mWater = std::make_unique<Water>(parent, sceneRoot, resourceSystem, ico);

        // Initialize FFT ocean simulation
        mOceanFFT = std::make_unique<Ocean::OceanFFTSimulation>();
        if (mFFTOceanEnabled)
        {
            if (mOceanFFT->initialize())
            {
                Log(Debug::Info) << "FFT Ocean simulation initialized successfully";
            }
            else
            {
                Log(Debug::Warning) << "FFT Ocean simulation initialization failed, falling back to legacy rendering";
                mFFTOceanEnabled = false;
            }
        }

        Log(Debug::Info) << "WaterManager initialized with legacy water renderer and FFT ocean system";
    }

    WaterManager::~WaterManager()
    {
    }

    void WaterManager::setCullCallback(osg::Callback* callback)
    {
        if (mWater)
            mWater->setCullCallback(callback);
    }

    void WaterManager::listAssetsToPreload(std::vector<VFS::Path::Normalized>& textures)
    {
        if (mWater)
            mWater->listAssetsToPreload(textures);
    }

    void WaterManager::setEnabled(bool enabled)
    {
        if (mWater)
            mWater->setEnabled(enabled);
    }

    bool WaterManager::toggle()
    {
        if (mWater)
            return mWater->toggle();
        return false;
    }

    bool WaterManager::isUnderwater(const osg::Vec3f& pos) const
    {
        if (mWater)
            return mWater->isUnderwater(pos);
        return false;
    }

    void WaterManager::addEmitter(const MWWorld::Ptr& ptr, float scale, float force)
    {
        if (mWater)
            mWater->addEmitter(ptr, scale, force);
    }

    void WaterManager::removeEmitter(const MWWorld::Ptr& ptr)
    {
        if (mWater)
            mWater->removeEmitter(ptr);
    }

    void WaterManager::updateEmitterPtr(const MWWorld::Ptr& old, const MWWorld::Ptr& ptr)
    {
        if (mWater)
            mWater->updateEmitterPtr(old, ptr);
    }

    void WaterManager::emitRipple(const osg::Vec3f& pos)
    {
        if (mWater)
            mWater->emitRipple(pos);
    }

    void WaterManager::removeCell(const MWWorld::CellStore* store)
    {
        if (mWater)
            mWater->removeCell(store);
    }

    void WaterManager::clearRipples()
    {
        if (mWater)
            mWater->clearRipples();
    }

    void WaterManager::changeCell(const MWWorld::CellStore* store)
    {
        mCurrentCell = store;

        // Classify the water type for this cell
        if (store)
        {
            mCurrentWaterType = mWaterTypeClassifier.classifyCell(store);
            Log(Debug::Info) << "Cell water type: " << Ocean::waterTypeToString(mCurrentWaterType);
        }
        else
        {
            mCurrentWaterType = Ocean::WaterType::INDOOR;
        }

        // Update the water renderer
        if (mWater)
            mWater->changeCell(store);
    }

    void WaterManager::setHeight(const float height)
    {
        if (mWater)
            mWater->setHeight(height);
    }

    void WaterManager::setRainIntensity(const float rainIntensity)
    {
        if (mWater)
            mWater->setRainIntensity(rainIntensity);
    }

    void WaterManager::setRainRipplesEnabled(bool enableRipples)
    {
        if (mWater)
            mWater->setRainRipplesEnabled(enableRipples);
    }

    void WaterManager::update(float dt, bool paused)
    {
        if (mWater)
            mWater->update(dt, paused);

        // Update FFT ocean simulation
        if (mFFTOceanEnabled && mOceanFFT && !paused)
        {
            // Only update for ocean cells
            if (mCurrentWaterType == Ocean::WaterType::OCEAN)
            {
                mOceanFFT->update(dt);
            }
        }
    }

    osg::Vec3d WaterManager::getPosition() const
    {
        if (mWater)
            return mWater->getPosition();
        return osg::Vec3d();
    }

    void WaterManager::processChangedSettings(const Settings::CategorySettingVector& settings)
    {
        if (mWater)
            mWater->processChangedSettings(settings);
    }

    void WaterManager::showWorld(bool show)
    {
        if (mWater)
            mWater->showWorld(show);
    }

    Ocean::WaterType WaterManager::getWaterType(const MWWorld::CellStore* cell) const
    {
        return mWaterTypeClassifier.classifyCell(cell);
    }

    void WaterManager::setFFTOceanEnabled(bool enabled)
    {
        mFFTOceanEnabled = enabled;

        if (enabled && mOceanFFT && !mOceanFFT->initialize())
        {
            Log(Debug::Warning) << "Failed to enable FFT ocean, compute shaders may not be supported";
            mFFTOceanEnabled = false;
        }

        Log(Debug::Info) << "FFT Ocean waves " << (mFFTOceanEnabled ? "enabled" : "disabled");
    }
}
