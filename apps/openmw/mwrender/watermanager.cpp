#include "watermanager.hpp"

#include "oceanwaterrenderer.hpp"
#include "water.hpp"

#include <components/debug/debuglog.hpp>

#include "../mwworld/cellstore.hpp"
#include "../mwmechanics/actorutil.hpp"

namespace MWRender
{
    WaterManager::WaterManager(osg::Group* parent, osg::Group* sceneRoot, Resource::ResourceSystem* resourceSystem,
        osgUtil::IncrementalCompileOperation* ico)
        : mParent(parent)
        , mSceneRoot(sceneRoot)
        , mResourceSystem(resourceSystem)
        , mIncrementalCompileOperation(ico)
        , mFFTOceanEnabled(false)  // Will be enabled if FFT initialization succeeds
    {
        // Create the legacy water renderer for lakes/rivers/interiors
        mWater = std::make_unique<Water>(parent, sceneRoot, resourceSystem, ico);

        // Create FFT ocean simulation
        mOceanFFT = std::make_unique<Ocean::OceanFFTSimulation>(resourceSystem);

        // Create ocean renderer with FFT simulation
        mOceanRenderer = std::make_unique<OceanWaterRenderer>(
            mSceneRoot, resourceSystem, mOceanFFT.get());

        // Try to initialize FFT ocean waves (disabled by default due to compute shader compilation issues)
        // TODO: Re-enable once compute shader issues are resolved
        // setFFTOceanEnabled(true);
        setFFTOceanEnabled(false);  // Force simple Gerstner shaders for now

        Log(Debug::Info) << "WaterManager initialized with legacy water and "
                         << (mFFTOceanEnabled ? "FFT" : "simple Gerstner") << " ocean system";
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
        mWaterEnabled = enabled;

        // Update renderers based on current water type and master enabled state
        bool useOcean = enabled && mFFTOceanEnabled && (mCurrentWaterType == Ocean::WaterType::OCEAN);

        if (mOceanRenderer)
            mOceanRenderer->setEnabled(useOcean);

        if (mWater)
            mWater->setEnabled(enabled && !useOcean);
    }

    bool WaterManager::toggle()
    {
        mWaterEnabled = !mWaterEnabled;

        // Update renderers based on new state
        setEnabled(mWaterEnabled);

        return mWaterEnabled;
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

        Log(Debug::Warning) << "========================================";
        Log(Debug::Warning) << "[WATERMGR] changeCell called";
        Log(Debug::Warning) << "[WATERMGR] Cell: " << (store ? (store->getCell()->isExterior() ? "EXTERIOR" : "INTERIOR") : "NULL");

        // Classify the water type for this cell
        if (store)
        {
            mCurrentWaterType = mWaterTypeClassifier.classifyCell(store);
            Log(Debug::Warning) << "[WATERMGR] Cell water type: " << Ocean::waterTypeToString(mCurrentWaterType);
        }
        else
        {
            mCurrentWaterType = Ocean::WaterType::INDOOR;
        }

        Log(Debug::Warning) << "[WATERMGR] mWaterEnabled: " << mWaterEnabled;
        Log(Debug::Warning) << "[WATERMGR] mFFTOceanEnabled: " << mFFTOceanEnabled;
        Log(Debug::Warning) << "[WATERMGR] mOceanRenderer exists: " << (mOceanRenderer ? "YES" : "NO");

        // Enable/disable ocean renderer based on water type and master enabled state
        bool useOcean = false;
        if (mOceanRenderer && mWaterEnabled)
        {
            useOcean = (mCurrentWaterType == Ocean::WaterType::OCEAN) && mFFTOceanEnabled;
            Log(Debug::Warning) << "[WATERMGR] useOcean decision: " << useOcean;
            Log(Debug::Warning) << "[WATERMGR] Calling ocean->setEnabled(" << useOcean << ")";
            mOceanRenderer->setEnabled(useOcean);
        }
        else if (mOceanRenderer)
        {
            Log(Debug::Warning) << "[WATERMGR] Disabling ocean (mWaterEnabled=" << mWaterEnabled << ")";
            mOceanRenderer->setEnabled(false);
        }

        Log(Debug::Warning) << "========================================";

        // Update the legacy water renderer (use for non-ocean water)
        if (mWater)
        {
            // Disable legacy water when using ocean renderer to avoid double rendering
            // Also respect master enabled state
            bool useLegacy = mWaterEnabled && !useOcean;
            mWater->setEnabled(useLegacy);
            mWater->changeCell(store);

            if (useLegacy)
            {
                Log(Debug::Verbose) << "Legacy water renderer active";
            }
            else if (useOcean)
            {
                Log(Debug::Verbose) << "Legacy water renderer disabled (using ocean)";
            }
            else
            {
                Log(Debug::Verbose) << "Legacy water renderer disabled (water globally disabled)";
            }
        }
    }

    void WaterManager::setHeight(const float height)
    {
        if (mWater)
            mWater->setHeight(height);

        if (mOceanRenderer)
            mOceanRenderer->setWaterHeight(height);
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

        // Update FFT ocean simulation and renderer
        if (mFFTOceanEnabled && mOceanFFT && !paused)
        {
            // Only update for ocean cells
            if (mCurrentWaterType == Ocean::WaterType::OCEAN)
            {
                mOceanFFT->update(dt);

                // Update ocean renderer with player position
                if (mOceanRenderer)
                {
                    MWWorld::Ptr player = MWMechanics::getPlayer();
                    if (!player.isEmpty())
                    {
                        osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
                        mOceanRenderer->update(dt, playerPos);
                    }
                }
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
