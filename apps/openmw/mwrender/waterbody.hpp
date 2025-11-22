#ifndef OPENMW_MWRENDER_WATERBODY_H
#define OPENMW_MWRENDER_WATERBODY_H

#include <osg/Vec3f>
#include <osg/ref_ptr>

namespace osg
{
    class Group;
    class Node;
}

namespace MWRender
{
    class WaterBody
    {
    public:
        virtual ~WaterBody() = default;

        virtual void setEnabled(bool enabled) = 0;
        virtual void update(float dt, bool paused, const osg::Vec3f& cameraPos) = 0;
        virtual void setHeight(float height) = 0;
        virtual bool isUnderwater(const osg::Vec3f& pos) const = 0;
        
        // Add to scene graph
        virtual void addToScene(osg::Group* parent) = 0;
        virtual void removeFromScene(osg::Group* parent) = 0;
    };
}

#endif
