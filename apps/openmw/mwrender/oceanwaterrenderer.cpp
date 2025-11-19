#include "oceanwaterrenderer.hpp"

#include <algorithm>
#include <string>

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
    void OceanUpdateCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        if (nv->getVisitorType() == osg::NodeVisitor::CULL_VISITOR)
        {
            osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(nv);
            osg::State* state = cv->getState();

            if (state && mFFT)
            {
                mFFT->dispatchCompute(state);
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
        Log(Debug::Warning) << "========================================";
        Log(Debug::Warning) << "[OCEAN] CONSTRUCTOR CALLED";
        Log(Debug::Warning) << "[OCEAN] Parent: " << parent;
        Log(Debug::Warning) << "[OCEAN] Parent name: " << (parent ? parent->getName() : "NULL");
        Log(Debug::Warning) << "========================================";

        mOceanNode = new osg::Group;
        mOceanNode->setName("Ocean Water");
        // FORCE VISIBILITY - use 0xffffffff to make it visible to everything
        mOceanNode->setNodeMask(0xffffffff);

        // Create Clipmap Geometry (large grid for ocean surface)
        // 256x256 grid provides good detail while maintaining performance
        mClipmapGeometry = createClipmapGeometry(256);

        osg::ref_ptr<osg::Geode> waterGeode = new osg::Geode;
        waterGeode->addDrawable(mClipmapGeometry);
        waterGeode->setName("Ocean Clipmap Geode");
        waterGeode->setNodeMask(0xffffffff);

        mClipmapTransform = new osg::PositionAttitudeTransform;
        mClipmapTransform->addChild(waterGeode);
        mClipmapTransform->setName("Ocean Clipmap Transform");
        mClipmapTransform->setNodeMask(0xffffffff);

        mOceanNode->addChild(mClipmapTransform);

        // Setup ocean shader (FFT waves)
        setupOceanShader();

        // Apply shader state to the water geode
        waterGeode->setStateSet(mOceanStateSet);

        // Install compute callback to dispatch FFT shaders
        if (mFFTSimulation)
        {
            mOceanNode->setCullCallback(new OceanUpdateCallback(mFFTSimulation));
            Log(Debug::Info) << "[OCEAN] FFT compute callback installed";
        }

        mParent->addChild(mOceanNode);

        Log(Debug::Warning) << "[OCEAN] Initialization complete";
        Log(Debug::Warning) << "[OCEAN] Ocean node added to parent: " << (mParent->containsNode(mOceanNode) ? "YES" : "NO");
        Log(Debug::Warning) << "[OCEAN] Ocean node children: " << mOceanNode->getNumChildren();
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

        // Update time uniform for wave animation
        static float accumulatedTime = 0.0f;
        accumulatedTime += dt;

        if (mOceanStateSet)
        {
            osg::Uniform* timeUniform = mOceanStateSet->getUniform("uTime");
            if (timeUniform)
                timeUniform->set(accumulatedTime);

            // Update camera position uniform
            osg::Uniform* camPosUniform = mOceanStateSet->getUniform("uCameraPosition");
            if (camPosUniform)
                camPosUniform->set(playerPos);
        }

        // Position clipmap with grid snapping to prevent swimming artifacts
        // Large mesh size to extend to horizon (128 cells * 8192 units/cell)
        float meshSize = 1048576.0f;  // ~128 Morrowind cells
        float gridSize = 256.0f;
        float vertexSpacing = meshSize / gridSize;

        float snappedX = std::floor(playerPos.x() / vertexSpacing) * vertexSpacing;
        float snappedY = std::floor(playerPos.y() / vertexSpacing) * vertexSpacing;

        mClipmapTransform->setPosition(osg::Vec3f(snappedX, snappedY, mWaterHeight));

        Log(Debug::Verbose) << "[OCEAN] Update - Time: " << accumulatedTime
                           << " | Player pos: " << playerPos.x() << "," << playerPos.y() << "," << playerPos.z()
                           << " | Water height: " << mWaterHeight;
    }

    void OceanWaterRenderer::setWaterHeight(float height)
    {
        mWaterHeight = height;
        if (mClipmapTransform)
        {
            osg::Vec3f pos = mClipmapTransform->getPosition();
            pos.z() = height;
            mClipmapTransform->setPosition(pos);

            Log(Debug::Warning) << "[OCEAN] *** Water height set to: " << height
                              << " | Clipmap pos: " << pos.x() << "," << pos.y() << "," << pos.z();
        }
        else
        {
            Log(Debug::Warning) << "[OCEAN] *** Water height set to: " << height << " but transform is NULL!";
        }
    }

    void OceanWaterRenderer::setEnabled(bool enabled)
    {
        mEnabled = enabled;
        if (mOceanNode)
        {
            // FORCE FULL VISIBILITY FOR TESTING
            mOceanNode->setNodeMask(enabled ? 0xffffffff : 0);
            Log(Debug::Warning) << "========================================";
            Log(Debug::Warning) << "[OCEAN] Ocean renderer " << (enabled ? "**ENABLED**" : "**DISABLED**");
            Log(Debug::Warning) << "[OCEAN] Node mask: " << std::hex << (enabled ? 0xffffffff : 0) << std::dec;
            Log(Debug::Warning) << "[OCEAN] Geometry vertices: " << (mClipmapGeometry ? mClipmapGeometry->getVertexArray()->getNumElements() : 0);
            Log(Debug::Warning) << "[OCEAN] Program valid: " << (mOceanProgram ? "YES" : "NO");
            Log(Debug::Warning) << "========================================";
        }
    }

    osg::ref_ptr<osg::Geometry> OceanWaterRenderer::createClipmapGeometry(int gridSize)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;

        // Create a grid of vertices
        // The mesh covers a large area extending to the horizon (~128 cells)
        float meshSize = 1048576.0f;  // ~128 Morrowind cells (1024km)
        float vertexSpacing = meshSize / gridSize;

        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;

        // Generate grid vertices
        for (int y = 0; y <= gridSize; ++y)
        {
            for (int x = 0; x <= gridSize; ++x)
            {
                float px = (x - gridSize / 2.0f) * vertexSpacing;
                float py = (y - gridSize / 2.0f) * vertexSpacing;

                verts->push_back(osg::Vec3f(px, py, 0.0f));
                texCoords->push_back(osg::Vec2f(x / float(gridSize), y / float(gridSize)));
            }
        }

        geometry->setVertexArray(verts);
        geometry->setTexCoordArray(0, texCoords);

        // Default normal (will be calculated in vertex shader)
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
        normals->push_back(osg::Vec3f(0, 0, 1));
        geometry->setNormalArray(normals, osg::Array::BIND_OVERALL);

        // Create triangle strip indices for efficient rendering
        osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);

        for (int y = 0; y < gridSize; ++y)
        {
            for (int x = 0; x < gridSize; ++x)
            {
                int i0 = y * (gridSize + 1) + x;
                int i1 = i0 + 1;
                int i2 = i0 + (gridSize + 1);
                int i3 = i2 + 1;

                // Two triangles per quad
                indices->push_back(i0);
                indices->push_back(i2);
                indices->push_back(i1);

                indices->push_back(i1);
                indices->push_back(i2);
                indices->push_back(i3);
            }
        }

        geometry->addPrimitiveSet(indices);

        // Large bounding box to prevent culling
        geometry->setInitialBound(osg::BoundingBox(-100000, -100000, -10000, 100000, 100000, 10000));

        Log(Debug::Info) << "Created ocean clipmap geometry: " << gridSize << "x" << gridSize
                         << " (" << verts->size() << " vertices, " << indices->size() / 3 << " triangles)";

        return geometry;
    }

    // Callback to update the view matrix inverse uniform
    struct ViewMatrixCallback : public osg::Uniform::Callback
    {
        virtual void operator()(osg::Uniform* uniform, osg::NodeVisitor* nv)
        {
            if (nv->getVisitorType() == osg::NodeVisitor::CULL_VISITOR)
            {
                osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(nv);
                osg::Matrix viewMatrix = cv->getCurrentCamera()->getViewMatrix();
                uniform->set(osg::Matrix::inverse(viewMatrix));
            }
        }
    };

    void OceanWaterRenderer::setupOceanShader()
    {
        Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();

        // 1. Try to load FFT ocean shaders first
        Shader::ShaderManager::DefineMap defineMap;
        defineMap["radialFog"] = "1";
        defineMap["disableNormals"] = "0";

        // Check if FFT simulation is available and initialized
        bool useFFT = mFFTSimulation && mFFTSimulation->isInitialized();

        if (useFFT)
        {
            mOceanProgram = shaderMgr.getProgram("compatibility/ocean/ocean", defineMap);

            if (!mOceanProgram)
            {
                Log(Debug::Warning) << "[OCEAN] Failed to load FFT ocean shaders, trying simple fallback";
                useFFT = false;
            }
        }

        // Fallback to simple ocean shaders (Gerstner waves, no FFT)
        if (!useFFT)
        {
            Log(Debug::Info) << "[OCEAN] Using simple ocean shaders (Gerstner waves)";
            mOceanProgram = shaderMgr.getProgram("compatibility/ocean/ocean_simple", defineMap);

            if (!mOceanProgram)
            {
                Log(Debug::Error) << "[OCEAN] Failed to load both FFT and simple ocean shaders, using basic material";

                // Last resort: basic material
                mOceanStateSet = new osg::StateSet;
                osg::ref_ptr<osg::Material> material = new osg::Material;
                material->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(0.0f, 0.5f, 1.0f, 0.7f));
                material->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.0f, 0.3f, 0.7f, 1.0f));
                material->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
                material->setShininess(osg::Material::FRONT_AND_BACK, 128.0f);
                mOceanStateSet->setAttributeAndModes(material, osg::StateAttribute::ON);
                mOceanStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
                return;
            }
        }

        // 2. Create state set
        mOceanStateSet = new osg::StateSet;
        mOceanStateSet->setAttributeAndModes(mOceanProgram, osg::StateAttribute::ON);

        Log(Debug::Info) << (useFFT ? "[OCEAN] FFT" : "[OCEAN] Simple") << " shader loaded successfully";

        // 3. Bind FFT textures (3 cascades) - only if using FFT
        if (useFFT && mFFTSimulation)
        {
            int cascadeCount = mFFTSimulation->getCascadeCount();
            Log(Debug::Info) << "[OCEAN] Binding textures for " << cascadeCount << " cascades";

            for (int i = 0; i < cascadeCount; i++)
            {
                // Displacement textures (units 0-2)
                osg::Texture2D* dispTex = mFFTSimulation->getDisplacementTexture(i);
                if (dispTex)
                {
                    mOceanStateSet->setTextureAttributeAndModes(i, dispTex, osg::StateAttribute::ON);
                    std::string uniformName = "uDisplacementCascade" + std::to_string(i);
                    mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), i));
                    Log(Debug::Info) << "[OCEAN]   Displacement cascade " << i << " -> unit " << i;
                }

                // Normal textures (units 3-5)
                osg::Texture2D* normalTex = mFFTSimulation->getNormalTexture(i);
                if (normalTex)
                {
                    int unit = 3 + i;
                    mOceanStateSet->setTextureAttributeAndModes(unit, normalTex, osg::StateAttribute::ON);
                    std::string uniformName = "uNormalCascade" + std::to_string(i);
                    mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), unit));
                    Log(Debug::Info) << "[OCEAN]   Normal cascade " << i << " -> unit " << unit;
                }

                // Foam textures (units 6-8)
                osg::Texture2D* foamTex = mFFTSimulation->getFoamTexture(i);
                if (foamTex)
                {
                    int unit = 6 + i;
                    mOceanStateSet->setTextureAttributeAndModes(unit, foamTex, osg::StateAttribute::ON);
                    std::string uniformName = "uFoamCascade" + std::to_string(i);
                    mOceanStateSet->addUniform(new osg::Uniform(uniformName.c_str(), unit));
                    Log(Debug::Info) << "[OCEAN]   Foam cascade " << i << " -> unit " << unit;
                }

                // Cascade tile sizes
                float tileSize = mFFTSimulation->getCascadeTileSize(i);
                std::string sizeUniformName = "uCascadeTileSize" + std::to_string(i);
                mOceanStateSet->addUniform(new osg::Uniform(sizeUniformName.c_str(), tileSize));
                Log(Debug::Info) << "[OCEAN]   Cascade " << i << " tile size: " << tileSize;
            }
        }

        // 4. Set shader uniforms
        mOceanStateSet->addUniform(new osg::Uniform("uTime", 0.0f));
        mOceanStateSet->addUniform(new osg::Uniform("uWaveAmplitude", 1.0f));
        mOceanStateSet->addUniform(new osg::Uniform("uEnableOceanWaves", true));

        // Camera position uniform with callback
        osg::ref_ptr<osg::Uniform> cameraPos = new osg::Uniform("uCameraPosition", osg::Vec3f(0, 0, 0));
        mOceanStateSet->addUniform(cameraPos);

        // Note: osg_ViewMatrixInverse is automatically provided by OSG, don't add it manually

        // 5. Setup rendering state
        mOceanStateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
        mOceanStateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
        mOceanStateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

        osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc(
            osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::ONE_MINUS_SRC_ALPHA);
        mOceanStateSet->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

        osg::ref_ptr<osg::Depth> depth = new osg::Depth;
        depth->setWriteMask(false);
        depth->setFunction(osg::Depth::LEQUAL);
        mOceanStateSet->setAttributeAndModes(depth, osg::StateAttribute::ON);

        Log(Debug::Info) << "[OCEAN] FFT ocean shader setup complete with "
                         << (mFFTSimulation ? mFFTSimulation->getCascadeCount() : 0) << " cascades";
    }

    void OceanWaterRenderer::updateFFTTextures()
    {
        // Not needed for Gerstner waves - all wave calculation is done in shaders
    }
}
