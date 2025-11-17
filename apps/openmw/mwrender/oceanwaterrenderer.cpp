#include "oceanwaterrenderer.hpp"

#include <osg/Depth>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Material>
#include <osg/Texture2D>
#include <osgUtil/CullVisitor>

#include <components/debug/debuglog.hpp>
#include <components/misc/constants.hpp>
#include <components/ocean/oceanfftsimulation.hpp>
#include <components/ocean/watersubdivider.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/waterutil.hpp>
#include <components/shader/shadermanager.hpp>

#include "vismask.hpp"

namespace MWRender
{
    OceanFFTUpdateCallback::OceanFFTUpdateCallback(Ocean::OceanFFTSimulation* fftSimulation)
        : mFFTSimulation(fftSimulation)
        , mLastFrameNumber(0)
    {
    }

    void OceanFFTUpdateCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
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

    OceanWaterRenderer::OceanWaterRenderer(osg::Group* parent, Resource::ResourceSystem* resourceSystem,
                                          Ocean::OceanFFTSimulation* fftSimulation)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mFFTSimulation(fftSimulation)
        , mWaterHeight(0.0f)
        , mEnabled(true)
        , mLastPlayerPos(0, 0, 0)
    {
        mOceanNode = new osg::Group;
        mOceanNode->setName("Ocean Water");
        mOceanNode->setNodeMask(Mask_Water);

        // Attach cull callback to dispatch FFT compute shaders once per frame
        mOceanNode->setCullCallback(new OceanFFTUpdateCallback(fftSimulation));

        // Setup ocean shader
        setupOceanShader();

        mParent->addChild(mOceanNode);

        Log(Debug::Info) << "OceanWaterRenderer initialized";
    }

    OceanWaterRenderer::~OceanWaterRenderer()
    {
        if (mParent && mOceanNode)
            mParent->removeChild(mOceanNode);
    }

    void OceanWaterRenderer::update(float dt, const osg::Vec3f& playerPos)
    {
        if (!mEnabled)
            return;

        // Update subdivision tracker
        mSubdivisionTracker.update(osg::Vec2f(playerPos.x(), playerPos.y()));

        // Check if player moved significantly OR if chunks haven't been created yet
        float distMoved = (playerPos - mLastPlayerPos).length();
        bool needsChunkUpdate = (distMoved > 512.0f) || mChunks.empty();

        if (needsChunkUpdate)
        {
            updateChunks(playerPos);
            mLastPlayerPos = playerPos;
            Log(Debug::Info) << "Ocean chunks updated: " << mChunks.size() << " chunks active at player pos ("
                            << playerPos.x() << ", " << playerPos.y() << ", " << playerPos.z() << ")";
        }

        // Update FFT textures
        updateFFTTextures();
    }

    void OceanWaterRenderer::setWaterHeight(float height)
    {
        mWaterHeight = height;

        // Update all chunk positions
        for (auto& pair : mChunks)
        {
            if (pair.second.geode && pair.second.geometry)
            {
                osg::Vec3Array* verts = dynamic_cast<osg::Vec3Array*>(pair.second.geometry->getVertexArray());
                if (verts)
                {
                    // Update Z coordinate
                    for (size_t i = 0; i < verts->size(); ++i)
                    {
                        (*verts)[i].z() = height;
                    }
                    verts->dirty();
                }
            }
        }
    }

    void OceanWaterRenderer::setEnabled(bool enabled)
    {
        mEnabled = enabled;
        if (mOceanNode)
        {
            mOceanNode->setNodeMask(enabled ? Mask_Water : 0);
        }
    }

    osg::ref_ptr<osg::Geometry> OceanWaterRenderer::createBaseWaterGeometry(float chunkSize)
    {
        // Create a simple quad grid (40x40 quads like the original water)
        constexpr int segments = 40;

        return SceneUtil::createWaterGeometry(chunkSize, segments, chunkSize / segments);
    }

    osg::ref_ptr<osg::Geometry> OceanWaterRenderer::createWaterChunk(const osg::Vec2i& gridPos, int subdivisionLevel)
    {
        // Create base geometry
        osg::ref_ptr<osg::Geometry> baseGeom = createBaseWaterGeometry(CHUNK_SIZE);

        // Apply subdivision if needed
        if (subdivisionLevel > 0 && baseGeom)
        {
            osg::ref_ptr<osg::Geometry> subdivided = Ocean::WaterSubdivider::subdivide(baseGeom.get(), subdivisionLevel);
            if (subdivided)
            {
                baseGeom = subdivided;
            }
        }

        // Set up geometry properties
        if (baseGeom)
        {
            baseGeom->setUseDisplayList(false);
            baseGeom->setUseVertexBufferObjects(true);
            baseGeom->setDataVariance(osg::Object::STATIC);

            // Position the chunk
            osg::Vec3Array* verts = dynamic_cast<osg::Vec3Array*>(baseGeom->getVertexArray());
            if (verts)
            {
                float offsetX = gridPos.x() * CHUNK_SIZE;
                float offsetY = gridPos.y() * CHUNK_SIZE;

                for (size_t i = 0; i < verts->size(); ++i)
                {
                    (*verts)[i].x() += offsetX;
                    (*verts)[i].y() += offsetY;
                    (*verts)[i].z() = mWaterHeight;
                }
                verts->dirty();
            }
        }

        return baseGeom;
    }

    void OceanWaterRenderer::updateChunks(const osg::Vec3f& playerPos)
    {
        // Determine which chunks should be visible
        int playerGridX = static_cast<int>(std::floor(playerPos.x() / CHUNK_SIZE));
        int playerGridY = static_cast<int>(std::floor(playerPos.y() / CHUNK_SIZE));

        std::set<std::pair<int, int>> desiredChunks;

        // Create chunks in a grid around the player
        for (int dx = -CHUNK_GRID_RADIUS; dx <= CHUNK_GRID_RADIUS; ++dx)
        {
            for (int dy = -CHUNK_GRID_RADIUS; dy <= CHUNK_GRID_RADIUS; ++dy)
            {
                osg::Vec2i gridPos(playerGridX + dx, playerGridY + dy);
                desiredChunks.insert(std::make_pair(gridPos.x(), gridPos.y()));
            }
        }

        // Remove chunks that are too far
        std::vector<std::pair<int, int>> toRemove;
        for (const auto& pair : mChunks)
        {
            if (desiredChunks.find(pair.first) == desiredChunks.end())
            {
                toRemove.push_back(pair.first);
            }
        }

        for (const auto& key : toRemove)
        {
            if (mChunks[key].geode)
            {
                mOceanNode->removeChild(mChunks[key].geode);
            }
            mChunks.erase(key);
        }

        // Add new chunks
        for (const auto& chunkKey : desiredChunks)
        {
            if (mChunks.find(chunkKey) == mChunks.end())
            {
                osg::Vec2i gridPos(chunkKey.first, chunkKey.second);

                // Calculate subdivision level based on distance
                osg::Vec2f chunkCenter(
                    gridPos.x() * CHUNK_SIZE + CHUNK_SIZE / 2.0f,
                    gridPos.y() * CHUNK_SIZE + CHUNK_SIZE / 2.0f
                );
                float distanceToPlayer = (chunkCenter - osg::Vec2f(playerPos.x(), playerPos.y())).length();
                int subdivLevel = mSubdivisionTracker.getSubdivisionLevel(chunkCenter, distanceToPlayer);

                // Create chunk
                WaterChunk chunk;
                chunk.gridPos = gridPos;
                chunk.subdivisionLevel = subdivLevel;
                chunk.geometry = createWaterChunk(gridPos, subdivLevel);

                if (chunk.geometry)
                {
                    chunk.geode = new osg::Geode;
                    chunk.geode->addDrawable(chunk.geometry);
                    chunk.geode->setStateSet(mOceanStateSet);

                    mOceanNode->addChild(chunk.geode);
                    mChunks[chunkKey] = chunk;

                    // Track in subdivision tracker
                    mSubdivisionTracker.markChunkSubdivided(chunkCenter, subdivLevel);

                    Log(Debug::Verbose) << "Created ocean chunk at (" << gridPos.x() << ", " << gridPos.y()
                                       << ") with subdivision level " << subdivLevel;
                }
            }
        }
    }

    void OceanWaterRenderer::setupOceanShader()
    {
        if (!mResourceSystem)
            return;

        auto& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();

        // Load ocean shaders
        osg::ref_ptr<osg::Shader> vertexShader =
            shaderManager.getShader("compatibility/ocean/ocean.vert", {}, osg::Shader::VERTEX);
        osg::ref_ptr<osg::Shader> fragmentShader =
            shaderManager.getShader("compatibility/ocean/ocean.frag", {}, osg::Shader::FRAGMENT);

        if (!vertexShader)
        {
            Log(Debug::Error) << "Failed to load ocean vertex shader: compatibility/ocean/ocean.vert";
            return;
        }

        if (!fragmentShader)
        {
            Log(Debug::Error) << "Failed to load ocean fragment shader: compatibility/ocean/ocean.frag";
            return;
        }

        mOceanProgram = shaderManager.getProgram(vertexShader, fragmentShader);

        if (!mOceanProgram)
        {
            Log(Debug::Error) << "Failed to create ocean shader program";
            return;
        }

        mOceanStateSet = new osg::StateSet;
        mOceanStateSet->setAttributeAndModes(mOceanProgram, osg::StateAttribute::ON);

        // Add uniforms for FFT textures
        mOceanStateSet->addUniform(new osg::Uniform("uDisplacementCascade0", 0));
        mOceanStateSet->addUniform(new osg::Uniform("uDisplacementCascade1", 1));
        mOceanStateSet->addUniform(new osg::Uniform("uDisplacementCascade2", 2));
        mOceanStateSet->addUniform(new osg::Uniform("uNormalCascade0", 3));
        mOceanStateSet->addUniform(new osg::Uniform("uNormalCascade1", 4));
        mOceanStateSet->addUniform(new osg::Uniform("uNormalCascade2", 5));
        mOceanStateSet->addUniform(new osg::Uniform("uFoamCascade0", 6));
        mOceanStateSet->addUniform(new osg::Uniform("uFoamCascade1", 7));

        // Wave parameters
        mOceanStateSet->addUniform(new osg::Uniform("uEnableOceanWaves", true));
        mOceanStateSet->addUniform(new osg::Uniform("uWaveAmplitude", 1.0f));

        // Water appearance
        mOceanStateSet->addUniform(new osg::Uniform("uDeepWaterColor", osg::Vec3f(0.0f, 0.2f, 0.3f)));
        mOceanStateSet->addUniform(new osg::Uniform("uShallowWaterColor", osg::Vec3f(0.0f, 0.4f, 0.5f)));
        mOceanStateSet->addUniform(new osg::Uniform("uWaterAlpha", 0.8f));

        Log(Debug::Info) << "Ocean shaders loaded successfully";
    }

    void OceanWaterRenderer::updateFFTTextures()
    {
        if (!mFFTSimulation || !mOceanStateSet)
            return;

        // Bind displacement textures
        for (int i = 0; i < 3; ++i)
        {
            osg::Texture2D* dispTex = mFFTSimulation->getDisplacementTexture(i);
            if (dispTex)
            {
                mOceanStateSet->setTextureAttributeAndModes(i, dispTex, osg::StateAttribute::ON);

                // Update cascade tile size uniform
                float tileSize = mFFTSimulation->getCascadeTileSize(i);
                std::string uniformName = "uCascadeTileSize" + std::to_string(i);
                osg::Uniform* uniform = mOceanStateSet->getUniform(uniformName);
                if (!uniform)
                {
                    uniform = new osg::Uniform(uniformName.c_str(), tileSize);
                    mOceanStateSet->addUniform(uniform);
                }
                else
                {
                    uniform->set(tileSize);
                }
            }
        }

        // Bind normal textures
        for (int i = 0; i < 3; ++i)
        {
            osg::Texture2D* normalTex = mFFTSimulation->getNormalTexture(i);
            if (normalTex)
            {
                mOceanStateSet->setTextureAttributeAndModes(3 + i, normalTex, osg::StateAttribute::ON);
            }
        }

        // Bind foam textures
        for (int i = 0; i < 2; ++i)
        {
            osg::Texture2D* foamTex = mFFTSimulation->getFoamTexture(i);
            if (foamTex)
            {
                mOceanStateSet->setTextureAttributeAndModes(6 + i, foamTex, osg::StateAttribute::ON);
            }
        }
    }
}
