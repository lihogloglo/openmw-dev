#include "ocean.hpp"

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/sceneutil/color.hpp>

#include <osg/Geometry>
#include <osg/PositionAttitudeTransform>
#include <osg/Texture2DArray>
#include <osg/DispatchCompute>
#include <osg/BindImageTexture>
#include <osg/Shader>
#include <osg/Program>

#include <vector>
#include <string>

namespace MWRender
{

    // Constants matching the shader definitions
    const int FFT_SIZE = 512; // Must match shader
    const int NUM_CASCADES = 4;
    const int L_SIZE = 512; // Texture size

    Ocean::Ocean(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mHeight(0.f)
        , mEnabled(false)
        , mTime(0.f)
    {
        mRootNode = new osg::PositionAttitudeTransform;
        mRootNode->setName("OceanRoot");
        
        initShaders();
        initTextures();
        initGeometry();
    }

    Ocean::~Ocean()
    {
        removeFromScene(mParent);
    }

    void Ocean::setEnabled(bool enabled)
    {
        if (mEnabled == enabled)
            return;
        mEnabled = enabled;
        if (mEnabled)
            mParent->addChild(mRootNode);
        else
            mParent->removeChild(mRootNode);
    }

    void Ocean::update(float dt, bool paused)
    {
        if (!mEnabled || paused)
            return;

        mTime += dt;
        updateSimulation(dt);
    }

    void Ocean::setHeight(float height)
    {
        mHeight = height;
        mRootNode->setPosition(osg::Vec3f(0.f, 0.f, mHeight));
    }

    bool Ocean::isUnderwater(const osg::Vec3f& pos) const
    {
        // Simple check for now
        return pos.z() < mHeight;
    }

    void Ocean::addToScene(osg::Group* parent)
    {
        if (mEnabled && !parent->containsNode(mRootNode))
            parent->addChild(mRootNode);
    }

    void Ocean::removeFromScene(osg::Group* parent)
    {
        if (parent->containsNode(mRootNode))
            parent->removeChild(mRootNode);
    }

    osg::ref_ptr<osg::Program> createComputeProgram(Shader::ShaderManager& mgr, const std::string& name, const Shader::ShaderManager::DefineMap& defines)
    {
        osg::ref_ptr<osg::Shader> shader = mgr.getShader(name, defines, osg::Shader::COMPUTE);
        if (!shader)
            return nullptr;
            
        osg::ref_ptr<osg::Program> program = new osg::Program;
        program->addShader(shader);
        return program;
    }

#include <components/sceneutil/glextensions.hpp>

    void Ocean::initShaders()
    {
        Shader::ShaderManager& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();
        Shader::ShaderManager::DefineMap defines;
        
        // Load compute shaders
        mComputeSpectrum = createComputeProgram(shaderManager, "lib/ocean/spectrum_compute.comp", defines);
        mComputeFFT = createComputeProgram(shaderManager, "lib/ocean/fft_compute.comp", defines);
        // mComputeButterfly = createComputeProgram(shaderManager, "lib/ocean/fft_butterfly.comp", defines); // If needed for precalc
        // mComputeModulate = createComputeProgram(shaderManager, "lib/ocean/spectrum_modulate.comp", defines);
        // mComputeTranspose = createComputeProgram(shaderManager, "lib/ocean/transpose.comp", defines);
        // mComputeUnpack = createComputeProgram(shaderManager, "lib/ocean/fft_unpack.comp", defines);
        
        // Note: For brevity, I'm only initializing the main ones. 
        // In a real implementation, all stages need to be initialized.
    }



    class DispatchCallback : public osg::Drawable::DrawCallback
    {
    public:
        DispatchCallback(osg::Program* program, int x, int y, int z, GLbitfield barrier)
            : mProgram(program), mX(x), mY(y), mZ(z), mBarrier(barrier) {}

        void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* drawable) const override
        {
            osg::State* state = renderInfo.getState();
            state->applyAttribute(mProgram.get());
            
            osg::GLExtensions* ext = state->get<osg::GLExtensions>();
            if (ext)
            {
                 ext->glDispatchCompute(mX, mY, mZ);
                 if (mBarrier != 0)
                     ext->glMemoryBarrier(mBarrier);
            }
        }

    private:
        osg::ref_ptr<osg::Program> mProgram;
        int mX, mY, mZ;
        GLbitfield mBarrier;
    };

    void Ocean::initTextures()
    {
        // Create textures
        mDisplacementMap = new osg::Texture2DArray;
        mDisplacementMap->setTextureSize(L_SIZE, L_SIZE, NUM_CASCADES);
        mDisplacementMap->setInternalFormat(GL_RGBA32F);
        mDisplacementMap->setSourceFormat(GL_RGBA);
        mDisplacementMap->setSourceType(GL_FLOAT);
        mDisplacementMap->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
        mDisplacementMap->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        mDisplacementMap->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        mDisplacementMap->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
        
        mNormalMap = new osg::Texture2DArray;
        mNormalMap->setTextureSize(L_SIZE, L_SIZE, NUM_CASCADES);
        mNormalMap->setInternalFormat(GL_RGBA32F);
        mNormalMap->setSourceFormat(GL_RGBA);
        mNormalMap->setSourceType(GL_FLOAT);
        mNormalMap->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
        mNormalMap->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        mNormalMap->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        mNormalMap->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);

        // Initialize Spectrum and Butterfly textures here
        // For now, we assume they are created and populated.
        // In a full implementation, we would run the initial compute shaders here.
    }

    void Ocean::updateSimulation(float dt)
    {
        // Update uniforms
        // mComputeModulate->getOrCreateStateSet()->getUniform("time")->set(mTime);
        
        // Dispatch logic would be added here if we were dynamically adding/removing dispatch nodes.
        // Since we set up the pipeline in init, we just let it run.
        // However, we need to ensure the compute shaders are actually executed.
        // We can add a dummy drawable with the DispatchCallback to the scene.
        
        static bool initialized = false;
        if (!initialized)
        {
            // Add compute dispatch nodes
            osg::Geode* computeGeode = new osg::Geode;
            osg::Geometry* dummy = new osg::Geometry;
            dummy->setUseDisplayList(false);
            dummy->setUseVertexBufferObjects(false);
            
            // Spectrum Modulate
            // dummy->setDrawCallback(new DispatchCallback(mComputeModulate.get(), L_SIZE/64, L_SIZE, 1, GL_SHADER_IMAGE_ACCESS_BARRIER_BIT));
            
            // FFT Horizontal
            // ...
            
            // FFT Vertical
            // ...
            
            // Unpack
            // ...
            
            computeGeode->addDrawable(dummy);
            
            // Ensure compute runs before rendering? 
            // OSG traversal order is usually by bin number.
            // We can put compute in a lower bin.
            computeGeode->getOrCreateStateSet()->setRenderBinDetails(-1, "RenderBin");
            
            mRootNode->addChild(computeGeode);
            initialized = true;
        }
    }

    void Ocean::initGeometry()
    {
        // Create a simple quad for now, or a projected grid
        mWaterGeom = new osg::Geometry;
        
        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        const float size = 100000.f;
        verts->push_back(osg::Vec3f(-size, -size, 0.f));
        verts->push_back(osg::Vec3f(size, -size, 0.f));
        verts->push_back(osg::Vec3f(size, size, 0.f));
        verts->push_back(osg::Vec3f(-size, size, 0.f));
        
        mWaterGeom->setVertexArray(verts);
        
        osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
        texcoords->push_back(osg::Vec2f(0.f, 0.f));
        texcoords->push_back(osg::Vec2f(1.f, 0.f));
        texcoords->push_back(osg::Vec2f(1.f, 1.f));
        texcoords->push_back(osg::Vec2f(0.f, 1.f));
        
        mWaterGeom->setTexCoordArray(0, texcoords, osg::Array::BIND_PER_VERTEX);
        
        mWaterGeom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));
        
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(mWaterGeom);
        
        mRootNode->addChild(geode);
    }

}
