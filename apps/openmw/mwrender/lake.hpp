#ifndef OPENMW_MWRENDER_LAKE_H
#define OPENMW_MWRENDER_LAKE_H

#include "waterbody.hpp"

#include <osg/ref_ptr>
#include <osg/Vec3f>
#include <osg/Callback>
#include <map>

namespace osg
{
    class Geometry;
    class PositionAttitudeTransform;
    class StateSet;
    class Texture2D;
    class TextureCubeMap;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    class WaterManager;

    class Lake : public WaterBody
    {
    public:
        Lake(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~Lake() override;

        void setEnabled(bool enabled) override;
        void update(float dt, bool paused, const osg::Vec3f& cameraPos) override;
        void setHeight(float height) override;  // Sets default height only
        bool isUnderwater(const osg::Vec3f& pos) const override;

        void addToScene(osg::Group* parent) override;
        void removeFromScene(osg::Group* parent) override;

        // Per-cell water management
        void addWaterCell(int gridX, int gridY, float height);
        void removeWaterCell(int gridX, int gridY);
        void clearAllCells();
        float getWaterHeightAt(const osg::Vec3f& pos) const;

        // Per-cell visibility control (for integration with cell loading)
        void showWaterCell(int gridX, int gridY);
        void hideWaterCell(int gridX, int gridY);

        // SSR/Cubemap reflection system integration
        void setWaterManager(WaterManager* waterManager);

    private:
        struct CellWater
        {
            int gridX, gridY;
            float height;
            osg::ref_ptr<osg::PositionAttitudeTransform> transform;
            osg::ref_ptr<osg::Geometry> geometry;
        };

        void createCellGeometry(CellWater& cell);
        osg::ref_ptr<osg::StateSet> createWaterStateSet();

        osg::ref_ptr<osg::Group> mParent;
        Resource::ResourceSystem* mResourceSystem;
        WaterManager* mWaterManager;  // For SSR/cubemap access (non-owning)

        osg::ref_ptr<osg::PositionAttitudeTransform> mRootNode;
        std::map<std::pair<int, int>, CellWater> mCellWaters;
        osg::ref_ptr<osg::StateSet> mWaterStateSet;
        osg::ref_ptr<osg::Callback> mStateSetUpdater;

        float mDefaultHeight;
        bool mEnabled;
    };
}

#endif
