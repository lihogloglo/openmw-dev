#ifndef OPENMW_MWRENDER_OCEAN_H
#define OPENMW_MWRENDER_OCEAN_H

#include "waterbody.hpp"

#include <osg/Vec3f>
#include <osg/ref_ptr>
#include <osg/Texture2DArray>
#include <osg/Program>
#include <osg/Uniform>

#include <memory>
#include <vector>

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
    class Ocean : public WaterBody
    {
    public:
        Ocean(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~Ocean() override;

        void setEnabled(bool enabled) override;
        void update(float dt, bool paused) override;
        void setHeight(float height) override;
        bool isUnderwater(const osg::Vec3f& pos) const override;

        void addToScene(osg::Group* parent) override;
        void removeFromScene(osg::Group* parent) override;

    private:
        void initShaders();
        void initTextures();
        void initGeometry();
        void updateSimulation(float dt);

        osg::ref_ptr<osg::Group> mParent;
        Resource::ResourceSystem* mResourceSystem;
        
        osg::ref_ptr<osg::PositionAttitudeTransform> mRootNode;
        osg::ref_ptr<osg::Geometry> mWaterGeom;
        
        // FFT Resources
        osg::ref_ptr<osg::Texture2DArray> mSpectrum;
        osg::ref_ptr<osg::Texture2DArray> mDisplacementMap;
        osg::ref_ptr<osg::Texture2DArray> mNormalMap;
        
        osg::ref_ptr<osg::Program> mComputeSpectrum;
        osg::ref_ptr<osg::Program> mComputeFFT;
        // ... other compute programs
        
        float mHeight;
        bool mEnabled;
        float mTime;
    };
}

#endif
