#ifndef OPENMW_MWRENDER_OCEAN_H
#define OPENMW_MWRENDER_OCEAN_H

#include "waterbody.hpp"

#include <osg/Vec3f>
#include <osg/ref_ptr>
#include <osg/Texture2DArray>
#include <osg/Program>
#include <osg/Uniform>
#include <osg/BufferObject>

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
        void update(float dt, bool paused, const osg::Vec3f& cameraPos) override;
        void setHeight(float height) override;
        bool isUnderwater(const osg::Vec3f& pos) const override;

        void addToScene(osg::Group* parent) override;
        void removeFromScene(osg::Group* parent) override;

        // Called from compute dispatch callback
        void dispatchCompute(osg::State* state);

    private:
        void initShaders();
        void initTextures();
        void initBuffers();
        void initGeometry();
        void initializeComputePipeline();
        void initializeComputeShaders(osg::State* state, osg::GLExtensions* ext, unsigned int contextID);
        void updateComputeShaders(osg::State* state, osg::GLExtensions* ext, unsigned int contextID);

        osg::ref_ptr<osg::Group> mParent;
        Resource::ResourceSystem* mResourceSystem;

        osg::ref_ptr<osg::PositionAttitudeTransform> mRootNode;
        osg::ref_ptr<osg::Geometry> mWaterGeom;
        osg::ref_ptr<osg::Uniform> mNodePositionUniform;

        // FFT Textures
        osg::ref_ptr<osg::Texture2DArray> mSpectrum;
        osg::ref_ptr<osg::Texture2DArray> mDisplacementMap;
        osg::ref_ptr<osg::Texture2DArray> mNormalMap;

        // FFT Buffers (SSBOs)
        osg::ref_ptr<osg::BufferObject> mButterflyBuffer;
        osg::ref_ptr<osg::BufferObject> mFFTBuffer;
        osg::ref_ptr<osg::BufferObject> mSpectrumBuffer;

        // Compute Shaders
        osg::ref_ptr<osg::Program> mComputeButterfly;
        osg::ref_ptr<osg::Program> mComputeSpectrum;
        osg::ref_ptr<osg::Program> mComputeModulate;
        osg::ref_ptr<osg::Program> mComputeFFT;
        osg::ref_ptr<osg::Program> mComputeTranspose;
        osg::ref_ptr<osg::Program> mComputeUnpack;

        float mHeight;
        bool mEnabled;
        float mTime;
        bool mInitialized;
    };
}

#endif
