#include "oceanwaterrenderer.hpp"

#include <osg/BlendFunc>
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
    OceanComputeDrawable::OceanComputeDrawable()
        : mFFTSimulation(nullptr)
        , mLastFrameNumber(0)
    {
        setSupportsDisplayList(false);
    }

    OceanComputeDrawable::OceanComputeDrawable(Ocean::OceanFFTSimulation* fftSimulation)
        : mFFTSimulation(fftSimulation)
        , mLastFrameNumber(0)
    {
        setSupportsDisplayList(false);
    }

    OceanComputeDrawable::OceanComputeDrawable(const OceanComputeDrawable& copy, const osg::CopyOp& copyop)
        : osg::Drawable(copy, copyop)
        , mFFTSimulation(copy.mFFTSimulation)
        , mLastFrameNumber(copy.mLastFrameNumber)
    {
    }

    void OceanComputeDrawable::drawImplementation(osg::RenderInfo& renderInfo) const
    {
        unsigned int frameNumber = renderInfo.getState()->getFrameStamp()->getFrameNumber();

        // Only dispatch compute shaders once per frame
        if (mFFTSimulation && frameNumber != mLastFrameNumber)
        {
            mFFTSimulation->dispatchCompute(renderInfo.getState());
            mLastFrameNumber = frameNumber;
        }
    }

    osg::BoundingBox OceanComputeDrawable::computeBoundingBox() const
    {
        return osg::BoundingBox(-100000.0f, -100000.0f, -10000.0f, 100000.0f, 100000.0f, 10000.0f);
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

        // Create a drawable to dispatch compute shaders during the Draw traversal
        // This ensures we have a valid OpenGL context
        mComputeGeode = new osg::Geode;
        mComputeGeode->setName("Ocean FFT Compute");
        mComputeGeode->addDrawable(new OceanComputeDrawable(fftSimulation));
        mComputeGeode->setCullingActive(false); // Always draw to ensure update happens

        // Set render bin to 0 (default/opaque) to ensure it runs before water rendering (bin 9/10)
        osg::StateSet* computeStateSet = mComputeGeode->getOrCreateStateSet();
        computeStateSet->setRenderBinDetails(0, "RenderBin");

        mOceanNode->addChild(mComputeGeode);

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
            Log(Debug::Info) << "[OCEAN DEBUG] Ocean chunks updated: " << mChunks.size() << " chunks active at player pos ("
                            << playerPos.x() << ", " << playerPos.y() << ", " << playerPos.z() << ")";
            
            if (!mChunks.empty()) {
                auto firstChunk = mChunks.begin();
                osg::Vec2i gridPos = firstChunk->second.gridPos;
                Log(Debug::Info) << "[OCEAN DEBUG] First chunk grid pos: (" << gridPos.x() << ", " << gridPos.y() << ")";
                if (firstChunk->second.geode) {
                     const osg::BoundingBox& bbox = firstChunk->second.geode->getBoundingBox();
                     Log(Debug::Info) << "[OCEAN DEBUG] First chunk bbox: " 
                         << bbox.xMin() << "," << bbox.yMin() << "," << bbox.zMin() << " -> "
                         << bbox.xMax() << "," << bbox.yMax() << "," << bbox.zMax();
                }
            }
        }

        // Update FFT textures
        updateFFTTextures();
    }

    void OceanWaterRenderer::setWaterHeight(float height)
    {
        if (std::abs(height - mWaterHeight) > 0.01f)
        {
            Log(Debug::Info) << "[OCEAN] Water height changed from " << mWaterHeight << " to " << height;
        }

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
        // Create a simple grid of triangles (40x40 segments)
        // We implement this manually instead of using SceneUtil::createWaterGeometry
        // because we need GL_TRIANGLES for the subdivider, but SceneUtil uses QUADS.
        constexpr int segments = 40;
        const float step = chunkSize / segments;
        const float textureRepeats = chunkSize / segments; // 1 repeat per segment
        const float texCoordStep = textureRepeats / segments;

        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
        osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);

        // Generate vertices
        for (int y = 0; y <= segments; ++y)
        {
            for (int x = 0; x <= segments; ++x)
            {
                float xPos = -chunkSize / 2.0f + x * step;
                float yPos = -chunkSize / 2.0f + y * step;
                verts->push_back(osg::Vec3f(xPos, yPos, 0.0f));

                float u = x * texCoordStep;
                float v = y * texCoordStep;
                texcoords->push_back(osg::Vec2f(u, v));
            }
        }

        // Generate indices for triangles
        for (int y = 0; y < segments; ++y)
        {
            for (int x = 0; x < segments; ++x)
            {
                // Grid vertex indices
                unsigned int i00 = y * (segments + 1) + x;
                unsigned int i10 = y * (segments + 1) + (x + 1);
                unsigned int i01 = (y + 1) * (segments + 1) + x;
                unsigned int i11 = (y + 1) * (segments + 1) + (x + 1);

                // Triangle 1 (00, 10, 11)
                indices->push_back(i00);
                indices->push_back(i10);
                indices->push_back(i11);

                // Triangle 2 (00, 11, 01)
                indices->push_back(i00);
                indices->push_back(i11);
                indices->push_back(i01);
            }
        }

        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setVertexArray(verts);
        geometry->setTexCoordArray(0, texcoords);
        
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
        normals->push_back(osg::Vec3f(0, 0, 1));
        geometry->setNormalArray(normals, osg::Array::BIND_OVERALL);

        geometry->addPrimitiveSet(indices);
        
        return geometry;
    }

    osg::ref_ptr<osg::Geometry> OceanWaterRenderer::createWaterChunk(const osg::Vec2i& gridPos, int subdivisionLevel)
    {
        // Create base geometry
        osg::ref_ptr<osg::Geometry> baseGeom = createBaseWaterGeometry(CHUNK_SIZE);

        if (!baseGeom || !baseGeom->getVertexArray() || baseGeom->getVertexArray()->getNumElements() == 0)
        {
            Log(Debug::Error) << "Failed to create base water geometry for chunk at ("
                             << gridPos.x() << ", " << gridPos.y() << ")";
            return nullptr;
        }

        Log(Debug::Verbose) << "Base geometry created with " << baseGeom->getVertexArray()->getNumElements()
                           << " vertices for chunk at (" << gridPos.x() << ", " << gridPos.y() << ")";

        // Apply subdivision if needed
        if (subdivisionLevel > 0 && baseGeom)
        {
            osg::ref_ptr<osg::Geometry> subdivided = Ocean::WaterSubdivider::subdivide(baseGeom.get(), subdivisionLevel);
            if (subdivided && subdivided->getVertexArray() && subdivided->getVertexArray()->getNumElements() > 0)
            {
                Log(Debug::Verbose) << "Subdivision level " << subdivisionLevel << " produced "
                                   << subdivided->getVertexArray()->getNumElements() << " vertices";
                baseGeom = subdivided;
            }
            else
            {
                Log(Debug::Warning) << "Subdivision level " << subdivisionLevel
                                   << " failed or produced empty geometry at grid pos ("
                                   << gridPos.x() << ", " << gridPos.y() << "), using base geometry";
            }
        }

        // Set up geometry properties
        if (baseGeom)
        {
            baseGeom->setUseDisplayList(false);
            baseGeom->setUseVertexBufferObjects(true);
            baseGeom->setDataVariance(osg::Object::STATIC);

            // Keep vertices in LOCAL space (don't bake world coordinates)
            // The transform node will handle positioning
            osg::Vec3Array* verts = dynamic_cast<osg::Vec3Array*>(baseGeom->getVertexArray());
            if (verts)
            {
                // Set Z to water height (in local space, this is the base height)
                for (size_t i = 0; i < verts->size(); ++i)
                {
                    (*verts)[i].z() = mWaterHeight;
                }
                verts->dirty();

                // CRITICAL FIX: Set a custom bounding box that accounts for wave displacement
                // The vertex shader displaces vertices, but the default bounding box is computed
                // from the CPU-side vertices (all at mWaterHeight), causing frustum culling issues
                // Max wave amplitude should be ~10m in production, but using 100m for safety
                const float MAX_WAVE_AMPLITUDE = 100.0f;
                osg::BoundingBox bbox;
                bbox.set(-CHUNK_SIZE/2.0f, -CHUNK_SIZE/2.0f, mWaterHeight - MAX_WAVE_AMPLITUDE,
                          CHUNK_SIZE/2.0f,  CHUNK_SIZE/2.0f, mWaterHeight + MAX_WAVE_AMPLITUDE);
                baseGeom->setInitialBound(bbox);

                Log(Debug::Verbose) << "Created water chunk at (" << gridPos.x() << ", " << gridPos.y()
                                   << ") with " << verts->size() << " vertices in local space";
            }
            else
            {
                Log(Debug::Error) << "Failed to get vertex array for water chunk at ("
                                 << gridPos.x() << ", " << gridPos.y() << ")";
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
            if (mChunks[key].transform)
            {
                mOceanNode->removeChild(mChunks[key].transform);
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
                    // Create geode to hold geometry
                    chunk.geode = new osg::Geode;
                    chunk.geode->addDrawable(chunk.geometry);
                    chunk.geode->setStateSet(mOceanStateSet);

                    // Create transform node to position chunk in world space
                    chunk.transform = new osg::PositionAttitudeTransform;
                    float offsetX = gridPos.x() * CHUNK_SIZE;
                    float offsetY = gridPos.y() * CHUNK_SIZE;
                    chunk.transform->setPosition(osg::Vec3f(offsetX, offsetY, 0.0f));
                    chunk.transform->addChild(chunk.geode);

                    mOceanNode->addChild(chunk.transform);
                    mChunks[chunkKey] = chunk;

                    // Track in subdivision tracker
                    mSubdivisionTracker.markChunkSubdivided(chunkCenter, subdivLevel);

                    Log(Debug::Verbose) << "Created ocean chunk at (" << gridPos.x() << ", " << gridPos.y()
                                       << ") with subdivision level " << subdivLevel
                                       << " at world pos (" << offsetX << ", " << offsetY << ")";
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

        // Enable alpha blending for water transparency
        mOceanStateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
        mOceanStateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

        // Configure blend function for proper transparency
        osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc(
            osg::BlendFunc::SRC_ALPHA,
            osg::BlendFunc::ONE_MINUS_SRC_ALPHA
        );
        mOceanStateSet->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

        // Enable depth testing but disable depth writing
        // Transparent water should not write to depth buffer to avoid occluding objects behind it
        // This matches the legacy water system behavior
        osg::ref_ptr<osg::Depth> depth = new osg::Depth;
        depth->setWriteMask(false);  // Don't write to depth buffer (transparent object)
        depth->setFunction(osg::Depth::LEQUAL);  // But still test against depth
        mOceanStateSet->setAttributeAndModes(depth, osg::StateAttribute::ON);

        // Add uniforms for FFT textures
        mOceanStateSet->addUniform(new osg::Uniform("uDisplacementCascade0", 0));
        mOceanStateSet->addUniform(new osg::Uniform("uDisplacementCascade1", 1));
        mOceanStateSet->addUniform(new osg::Uniform("uDisplacementCascade2", 2));
        mOceanStateSet->addUniform(new osg::Uniform("uNormalCascade0", 3));
        mOceanStateSet->addUniform(new osg::Uniform("uNormalCascade1", 4));
        mOceanStateSet->addUniform(new osg::Uniform("uNormalCascade2", 5));
        mOceanStateSet->addUniform(new osg::Uniform("uFoamCascade0", 6));
        mOceanStateSet->addUniform(new osg::Uniform("uFoamCascade1", 7));

        // Initialize cascade tile size uniforms immediately
        for (int i = 0; i < 3; ++i)
        {
            float tileSize = mFFTSimulation ? mFFTSimulation->getCascadeTileSize(i) : 50.0f * std::pow(4.0f, i);
            std::string uniformName = "uCascadeTileSize" + std::to_string(i);
            mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), tileSize));
        }

        // Wave parameters
        mOceanStateSet->addUniform(new osg::Uniform("uEnableOceanWaves", true));
        mOceanStateSet->addUniform(new osg::Uniform("uWaveAmplitude", 1.0f));  // Amplitude multiplier (FFT already in world units)

        // Water appearance
        mOceanStateSet->addUniform(new osg::Uniform("uDeepWaterColor", osg::Vec3f(0.0f, 0.2f, 0.3f)));
        mOceanStateSet->addUniform(new osg::Uniform("uShallowWaterColor", osg::Vec3f(0.0f, 0.4f, 0.5f)));
        mOceanStateSet->addUniform(new osg::Uniform("uWaterAlpha", 0.8f));

        Log(Debug::Info) << "Ocean shaders loaded successfully";
        Log(Debug::Info) << "[OCEAN DEBUG] uEnableOceanWaves=true, uWaveAmplitude=1.0";
    }

    void OceanWaterRenderer::updateFFTTextures()
    {
        if (!mFFTSimulation || !mOceanStateSet)
            return;

        static bool logged = false;
        if (!logged)
        {
            Log(Debug::Info) << "[OCEAN DEBUG] updateFFTTextures called - binding displacement textures";
            logged = true;
        }

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
                    Log(Debug::Info) << "[OCEAN DEBUG] Added uniform " << uniformName << " = " << tileSize;
                }
                else
                {
                    uniform->set(tileSize);
                }
            }
            else
            {
                Log(Debug::Warning) << "[OCEAN DEBUG] Displacement texture " << i << " is NULL!";
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
