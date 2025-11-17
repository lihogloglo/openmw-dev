#ifndef OPENMW_MWRENDER_WATERMANAGER_H
#define OPENMW_MWRENDER_WATERMANAGER_H

#include <memory>
#include <vector>

#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/ocean/watertype.hpp>
#include <components/ocean/oceanfftsimulation.hpp>

#include "watertypeclassifier.hpp"
#include <components/settings/settings.hpp>
#include <components/vfs/pathutil.hpp>

namespace osg
{
    class Group;
    class Callback;
}

namespace osgUtil
{
    class IncrementalCompileOperation;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWWorld
{
    class CellStore;
    class Ptr;
}

namespace MWRender
{
    class Water; // Legacy water renderer
    class OceanWaterRenderer;

    /// Manages multiple water renderers for different water types
    /// This class coordinates ocean, lake, and static water rendering
    class WaterManager
    {
    public:
        WaterManager(osg::Group* parent, osg::Group* sceneRoot, Resource::ResourceSystem* resourceSystem,
            osgUtil::IncrementalCompileOperation* ico);
        ~WaterManager();

        void setCullCallback(osg::Callback* callback);

        void listAssetsToPreload(std::vector<VFS::Path::Normalized>& textures);

        void setEnabled(bool enabled);

        bool toggle();

        bool isUnderwater(const osg::Vec3f& pos) const;

        /// adds an emitter, position will be tracked automatically using its scene node
        void addEmitter(const MWWorld::Ptr& ptr, float scale = 1.f, float force = 1.f);
        void removeEmitter(const MWWorld::Ptr& ptr);
        void updateEmitterPtr(const MWWorld::Ptr& old, const MWWorld::Ptr& ptr);
        void emitRipple(const osg::Vec3f& pos);

        void removeCell(const MWWorld::CellStore* store); ///< remove all emitters in this cell

        void clearRipples();

        void changeCell(const MWWorld::CellStore* store);
        void setHeight(const float height);
        void setRainIntensity(const float rainIntensity);
        void setRainRipplesEnabled(bool enableRipples);

        void update(float dt, bool paused);

        osg::Vec3d getPosition() const;

        void processChangedSettings(const Settings::CategorySettingVector& settings);

        void showWorld(bool show);

        /// Get the water type for a cell
        Ocean::WaterType getWaterType(const MWWorld::CellStore* cell) const;

        /// Get the FFT ocean simulation (if initialized)
        Ocean::OceanFFTSimulation* getOceanFFTSimulation() { return mOceanFFT.get(); }

        /// Enable/disable FFT ocean waves
        void setFFTOceanEnabled(bool enabled);

        /// Check if FFT ocean is enabled
        bool isFFTOceanEnabled() const { return mFFTOceanEnabled; }

    private:
        // Legacy water renderer (for non-ocean water)
        std::unique_ptr<Water> mWater;

        // Ocean water renderer (with FFT waves and subdivision)
        std::unique_ptr<OceanWaterRenderer> mOceanRenderer;

        // Water type classifier
        WaterTypeClassifier mWaterTypeClassifier;

        // FFT ocean simulation
        std::unique_ptr<Ocean::OceanFFTSimulation> mOceanFFT;
        bool mFFTOceanEnabled;

        // Current cell information
        const MWWorld::CellStore* mCurrentCell{ nullptr };
        Ocean::WaterType mCurrentWaterType{ Ocean::WaterType::INDOOR };

        // Resource references
        osg::ref_ptr<osg::Group> mParent;
        osg::ref_ptr<osg::Group> mSceneRoot;
        Resource::ResourceSystem* mResourceSystem;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> mIncrementalCompileOperation;
    };
}

#endif
