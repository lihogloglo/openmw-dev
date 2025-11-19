#ifndef OPENMW_MWRENDER_OCEANWATERRENDERER_H
#define OPENMW_MWRENDER_OCEANWATERRENDERER_H

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/PositionAttitudeTransform>
#include <osg/Program>
#include <osg/ref_ptr>

#include <map>
#include <vector>

namespace Ocean
{
    class OceanFFTSimulation;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    /// Callback to dispatch FFT compute shaders during the cull phase
    class OceanUpdateCallback : public osg::NodeCallback
    {
    public:
        OceanUpdateCallback(Ocean::OceanFFTSimulation* fft) : mFFT(fft) {}

        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) override;

    private:
        Ocean::OceanFFTSimulation* mFFT;
    };

    /// Renders ocean water with FFT waves and character-centered subdivision
    class OceanWaterRenderer
    {
    public:
        OceanWaterRenderer(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
                          Ocean::OceanFFTSimulation* fftSimulation);
        ~OceanWaterRenderer();

        void update(float dt, const osg::Vec3f& playerPos);
        void setWaterHeight(float height);
        void setEnabled(bool enabled);

    private:
        void setupOceanShader();
        void updateFFTTextures();
        
        // Clipmap geometry generation
        osg::ref_ptr<osg::Geometry> createClipmapGeometry(int gridSize);

        osg::Group* mParent;
        Resource::ResourceSystem* mResourceSystem;
        Ocean::OceanFFTSimulation* mFFTSimulation;

        osg::ref_ptr<osg::Group> mOceanNode;
        osg::ref_ptr<osg::Program> mOceanProgram;
        osg::ref_ptr<osg::StateSet> mOceanStateSet;

        // Clipmap components
        osg::ref_ptr<osg::Geometry> mClipmapGeometry;
        osg::ref_ptr<osg::PositionAttitudeTransform> mClipmapTransform;

        float mWaterHeight;
        bool mEnabled;
        osg::Vec3f mLastPlayerPos;
    };
}

#endif
