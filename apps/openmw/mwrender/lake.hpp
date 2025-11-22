#ifndef OPENMW_MWRENDER_LAKE_H
#define OPENMW_MWRENDER_LAKE_H

#include "waterbody.hpp"

#include <osg/ref_ptr>
#include <osg/Vec3f>

namespace osg
{
    class Geometry;
    class PositionAttitudeTransform;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    class Lake : public WaterBody
    {
    public:
        Lake(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~Lake() override;

        void setEnabled(bool enabled) override;
        void update(float dt, bool paused, const osg::Vec3f& cameraPos) override;
        void setHeight(float height) override;
        bool isUnderwater(const osg::Vec3f& pos) const override;

        void addToScene(osg::Group* parent) override;
        void removeFromScene(osg::Group* parent) override;

    private:
        void initGeometry();
        void initShaders();

        osg::ref_ptr<osg::Group> mParent;
        Resource::ResourceSystem* mResourceSystem;
        
        osg::ref_ptr<osg::PositionAttitudeTransform> mRootNode;
        osg::ref_ptr<osg::Geometry> mWaterGeom;
        
        float mHeight;
        bool mEnabled;
    };
}

#endif
