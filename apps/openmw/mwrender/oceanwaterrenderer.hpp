#ifndef OPENMW_MWRENDER_OCEANWATERRENDERER_H
#define OPENMW_MWRENDER_OCEANWATERRENDERER_H

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Program>
#include <osg/ref_ptr>

#include <components/ocean/watersubdivisiontracker.hpp>

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
    /// Cull callback to dispatch FFT compute shaders once per frame before rendering ocean
    class OceanFFTUpdateCallback : public osg::NodeCallback
    {
    public:
        OceanFFTUpdateCallback(Ocean::OceanFFTSimulation* fftSimulation)
            : mFFTSimulation(fftSimulation)
            , mLastFrameNumber(0)
        {
        }

        void operator()(osg::Node* node, osg::NodeVisitor* nv) override
        {
            osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>(nv);
            if (cv)
            {
                unsigned int frameNumber = cv->getFrameStamp() ? cv->getFrameStamp()->getFrameNumber() : 0;

                // Only dispatch compute shaders once per frame
                if (mFFTSimulation && frameNumber != mLastFrameNumber)
                {
                    osg::State* state = cv->getRenderStage()->getStateSet()
                        ? cv->getState()
                        : nullptr;

                    if (state)
                    {
                        mFFTSimulation->dispatchCompute(state);
                        mLastFrameNumber = frameNumber;
                    }
                }
            }

            traverse(node, nv);
        }

    private:
        Ocean::OceanFFTSimulation* mFFTSimulation;
        mutable unsigned int mLastFrameNumber;
    };

    /// Renders ocean water with FFT-based waves and character-centered subdivision
    class OceanWaterRenderer
    {
    public:
        OceanWaterRenderer(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
                          Ocean::OceanFFTSimulation* fftSimulation);
        ~OceanWaterRenderer();

        /// Update renderer (handle subdivision, player movement)
        void update(float dt, const osg::Vec3f& playerPos);

        /// Set water height
        void setWaterHeight(float height);

        /// Enable/disable ocean rendering
        void setEnabled(bool enabled);

        /// Get the ocean scene node
        osg::Group* getSceneNode() { return mOceanNode.get(); }

    private:
        /// Water chunk for character-centered rendering
        struct WaterChunk
        {
            osg::Vec2i gridPos;              // Grid position in cell coordinates
            int subdivisionLevel;             // 0-3
            osg::ref_ptr<osg::Geometry> geometry;
            osg::ref_ptr<osg::Geode> geode;
        };

        /// Create a water chunk with given subdivision level
        osg::ref_ptr<osg::Geometry> createWaterChunk(const osg::Vec2i& gridPos, int subdivisionLevel);

        /// Create base water geometry (40x40 quads)
        osg::ref_ptr<osg::Geometry> createBaseWaterGeometry(float chunkSize);

        /// Update chunks based on player position
        void updateChunks(const osg::Vec3f& playerPos);

        /// Setup ocean shader state
        void setupOceanShader();

        /// Bind FFT textures as uniforms
        void updateFFTTextures();

        // Scene structure
        osg::ref_ptr<osg::Group> mParent;
        osg::ref_ptr<osg::Group> mOceanNode;

        // Resources
        Resource::ResourceSystem* mResourceSystem;
        Ocean::OceanFFTSimulation* mFFTSimulation;

        // Shaders
        osg::ref_ptr<osg::Program> mOceanProgram;
        osg::ref_ptr<osg::StateSet> mOceanStateSet;

        // Subdivision tracking
        Ocean::WaterSubdivisionTracker mSubdivisionTracker;

        // Active water chunks
        std::map<std::pair<int, int>, WaterChunk> mChunks;

        // Parameters
        float mWaterHeight;
        bool mEnabled;
        osg::Vec3f mLastPlayerPos;

        // Configuration
        static constexpr float CHUNK_SIZE = 8192.0f; // Size of each chunk in world units
        static constexpr int CHUNK_GRID_RADIUS = 3;  // Render 7x7 chunks around player
    };
}

#endif
