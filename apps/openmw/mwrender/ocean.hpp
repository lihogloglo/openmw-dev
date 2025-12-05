#ifndef OPENMW_MWRENDER_OCEAN_H
#define OPENMW_MWRENDER_OCEAN_H

#include "waterbody.hpp"

#include <osg/Vec3f>
#include <osg/ref_ptr>
#include <osg/Texture2DArray>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
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

namespace osgUtil
{
    class CullVisitor;
}

namespace Resource
{
    class ResourceSystem;
}

namespace SceneUtil
{
    class RTTNode;
}

namespace MWRender
{
    class Ocean : public WaterBody
    {
    public:
        Ocean(osg::Group* parent, Resource::ResourceSystem* resourceSystem);

        // Set reflection/refraction resources (called by WaterManager)
        // Uses SceneUtil::RTTNode base class so we don't depend on water.cpp's local classes
        void setReflection(SceneUtil::RTTNode* reflection) { mReflection = reflection; }
        void setRefraction(SceneUtil::RTTNode* refraction) { mRefraction = refraction; }

        // Set shore distance map for vertex-level wave attenuation
        void setShoreDistanceMap(osg::Texture2D* texture, float minX, float minY, float maxX, float maxY);

        // SSR (Screen-Space Reflections) mode support
        // Set scene color buffer for SSR raymarching (provided by PostProcessor or similar)
        void setSceneColorBuffer(osg::Texture2D* texture);
        // Set environment cubemap for SSR fallback
        void setEnvironmentMap(osg::TextureCubeMap* cubemap);
        // Check if SSR mode is enabled
        bool isSSREnabled() const { return mUseSSR; }

        // Called by StateSetUpdater to bind textures dynamically
        void updateStateSet(osg::StateSet* stateset, osgUtil::CullVisitor* cv);
        ~Ocean() override;

        void setEnabled(bool enabled) override;
        void update(float dt, bool paused, const osg::Vec3f& cameraPos) override;
        void setHeight(float height) override;
        bool isUnderwater(const osg::Vec3f& pos) const override;

        void addToScene(osg::Group* parent) override;
        void removeFromScene(osg::Group* parent) override;

        // Called from compute dispatch callback
        void dispatchCompute(osg::State* state);

        // Debug visualization
        void setDebugVisualizeCascades(bool enabled);
        void setDebugVisualizeLOD(bool enabled);
        void setDebugVisualizeShore(bool enabled);

        // Runtime configurable parameters
        void setWaterColor(const osg::Vec3f& color);
        void setFoamColor(const osg::Vec3f& color);
        void setWindSpeed(float speed);
        void setWindDirection(float degrees);
        void setFetchLength(float length);
        void setSwell(float swell);
        void setDetail(float detail);
        void setSpread(float spread);
        void setFoamAmount(float amount);

        // Shore smoothing parameters
        void setShoreWaveAttenuation(float attenuation);
        void setShoreDepthScale(float scale);
        void setShoreFoamBoost(float boost);
        void setVertexShoreSmoothing(float smoothing);  // Controls vertex displacement reduction

        osg::Vec3f getWaterColor() const { return mWaterColor; }
        osg::Vec3f getFoamColor() const { return mFoamColor; }
        float getWindSpeed() const { return mWindSpeed; }
        float getWindDirection() const { return mWindDirection; }
        float getFetchLength() const { return mFetchLength; }
        float getSwell() const { return mSwell; }
        float getDetail() const { return mDetail; }
        float getSpread() const { return mSpread; }
        float getFoamAmount() const { return mFoamAmount; }
        float getShoreWaveAttenuation() const { return mShoreWaveAttenuation; }
        float getShoreDepthScale() const { return mShoreDepthScale; }
        float getShoreFoamBoost() const { return mShoreFoamBoost; }
        float getVertexShoreSmoothing() const { return mVertexShoreSmoothing; }

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
        osg::ref_ptr<osg::Uniform> mCameraPositionUniform;
        osg::ref_ptr<osg::Uniform> mDebugVisualizeCascadesUniform;
        osg::ref_ptr<osg::Uniform> mDebugVisualizeLODUniform;
        osg::ref_ptr<osg::Uniform> mDebugVisualizeShoreUniform;
        osg::ref_ptr<osg::Uniform> mWaterColorUniform;
        osg::ref_ptr<osg::Uniform> mFoamColorUniform;
        osg::ref_ptr<osg::Uniform> mShoreWaveAttenuationUniform;
        osg::ref_ptr<osg::Uniform> mShoreDepthScaleUniform;
        osg::ref_ptr<osg::Uniform> mShoreFoamBoostUniform;
        osg::ref_ptr<osg::Uniform> mVertexShoreSmoothingUniform;

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

        // Runtime configurable parameters (default values from Godot reference)
        osg::Vec3f mWaterColor;
        osg::Vec3f mFoamColor;
        float mWindSpeed;       // m/s
        float mWindDirection;   // degrees
        float mFetchLength;     // meters
        float mSwell;           // 0-2
        float mDetail;          // 0-1
        float mSpread;          // 0-1
        float mFoamAmount;      // 0-10
        float mShoreWaveAttenuation; // 0-1, how much waves are reduced at shore
        float mShoreDepthScale;      // MW units, depth at which waves reach full amplitude
        float mShoreFoamBoost;       // 0-5, extra foam intensity at shore
        float mVertexShoreSmoothing; // 0-1, manual vertex displacement reduction
        bool mNeedsSpectrumRegeneration;

        // Reflection/Refraction (provided by WaterManager)
        SceneUtil::RTTNode* mReflection;
        SceneUtil::RTTNode* mRefraction;

        // Shore distance map for vertex-level wave attenuation
        osg::ref_ptr<osg::Texture2D> mShoreDistanceMap;
        osg::ref_ptr<osg::Uniform> mShoreMapBoundsUniform;  // vec4(minX, minY, maxX, maxY)
        bool mHasShoreDistanceMap;

        // SSR (Screen-Space Reflections) mode
        bool mUseSSR;
        osg::ref_ptr<osg::Texture2D> mSceneColorBuffer;    // Scene color for SSR sampling
        osg::ref_ptr<osg::TextureCubeMap> mEnvironmentMap; // Cubemap fallback
        osg::ref_ptr<osg::Uniform> mSSRMixStrengthUniform;
    };
}

#endif
