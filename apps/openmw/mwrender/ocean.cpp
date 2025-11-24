#include "ocean.hpp"
#include "ocean.hpp"

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/sceneutil/color.hpp>
#include <components/sceneutil/depth.hpp>

#include "renderbin.hpp"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/PositionAttitudeTransform>
#include <osg/Texture2DArray>
#include <osg/BindImageTexture>
#include <osg/Shader>
#include <osg/Program>
#include <osg/BufferObject>
#include <osg/BufferIndexBinding>
#include <osg/Image>
#include <osg/GLExtensions>
#include <osg/State>
#include <osg/Array>
#include <osg/Depth>
#include <osg/Depth>
#include <osg/StateSet>
#include <osg/BlendFunc>

#include <osgUtil/CullVisitor>

#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif

namespace MWRender
{

    // Constants matching the shader definitions
    const int FFT_SIZE = 512;
    const int NUM_CASCADES = 4;
    const int L_SIZE = 512;
    const int NUM_SPECTRA = 4; // hx, hy, hz, and gradients

    // Morrowind unit conversion: 22.1 units = 1 foot, 1 meter = 3.28084 feet
    const float METERS_TO_MW_UNITS = 72.53f; // 22.1 * 3.28084

    // Calculate number of FFT stages (log2)
    int getNumStages(int size) {
        int stages = 0;
        while (size > 1) {
            size >>= 1;
            stages++;
        }
        return stages;
    }

    // Custom drawable callback for compute shader dispatch
    class ComputeDispatchCallback : public osg::Drawable::DrawCallback
    {
    public:
        ComputeDispatchCallback(Ocean* ocean) : mOcean(ocean) {}

        void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* drawable) const override
        {
            // Don't actually draw, just dispatch compute shaders
            if (mOcean) {
                // Reduced logging frequency to avoid spamming too much, but enough to verify it runs
                static int frameCount = 0;
                frameCount++;
                if (frameCount % 60 == 0) {
                     std::cout << "ComputeDispatchCallback::drawImplementation called (Frame " << frameCount << ")" << std::endl;
                }
                mOcean->dispatchCompute(renderInfo.getState());
            }
        }

    private:
        Ocean* mOcean;
    };

#include <iostream>

    // ... (existing code)

    Ocean::Ocean(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mHeight(0.f)
        , mEnabled(false)
        , mTime(0.f)
        , mInitialized(false)
    {
        std::cout << "Ocean::Ocean constructor called" << std::endl;
        mRootNode = new osg::PositionAttitudeTransform;
        mRootNode->setName("OceanRoot");

        initTextures();
        initBuffers();
        initShaders();
        initGeometry();

        // Initialize the compute pipeline once
        initializeComputePipeline();
        std::cout << "Ocean::Ocean constructor finished" << std::endl;
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
            addToScene(mParent);
        else
            removeFromScene(mParent);
    }

    bool Ocean::isUnderwater(const osg::Vec3f& pos) const
    {
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

    void Ocean::update(float dt, bool paused, const osg::Vec3f& cameraPos)
    {
        if (!paused)
            mTime += dt;
            
        // Debug logging for time and Z-following
        static float timer = 0.0f;
        timer += dt;
            osg::Vec3f pos = mRootNode->getPosition();
            std::cout << "Ocean Update: Time=" << mTime << " Height=" << mHeight << " RootZ=" << pos.z() << " CamZ=" << cameraPos.z() << std::endl;
            timer = 0.0f;


        // Clipmap Ocean: Keep mesh stationary, only update camera position for shader
        // Unlike traditional infinite ocean, clipmap mesh stays at origin (0,0,height)
        // The vertex shader will calculate world positions using camera offset
        // This prevents texture swimming caused by mesh movement

        // Keep mesh at origin (only set Z to sea level)
        mRootNode->setPosition(osg::Vec3f(0.f, 0.f, mHeight));

        // Pass camera position to shader for world-space calculations
        // The shader will use this to compute world UVs relative to a snapped grid
        if (mNodePositionUniform)
            mNodePositionUniform->set(osg::Vec3f(0.f, 0.f, mHeight));

        // Update camera position uniform for cascade selection
        if (mCameraPositionUniform)
            mCameraPositionUniform->set(cameraPos);
    }

    void Ocean::setHeight(float height)
    {
        mHeight = height;
        mRootNode->setPosition(osg::Vec3f(0.f, 0.f, mHeight));
        if (mNodePositionUniform)
            mNodePositionUniform->set(osg::Vec3f(0.f, 0.f, mHeight));
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

    void Ocean::initShaders()
    {
        std::cout << "Ocean::initShaders called" << std::endl;
        Shader::ShaderManager& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();
        Shader::ShaderManager::DefineMap defines;

        // Load all compute shaders
        mComputeButterfly = createComputeProgram(shaderManager, "lib/ocean/fft_butterfly.comp", defines);
        if (!mComputeButterfly) std::cerr << "Failed to load lib/ocean/fft_butterfly.comp" << std::endl;
        
        mComputeSpectrum = createComputeProgram(shaderManager, "lib/ocean/spectrum_compute.comp", defines);
        if (!mComputeSpectrum) std::cerr << "Failed to load lib/ocean/spectrum_compute.comp" << std::endl;

        mComputeModulate = createComputeProgram(shaderManager, "lib/ocean/spectrum_modulate.comp", defines);
        if (!mComputeModulate) std::cerr << "Failed to load lib/ocean/spectrum_modulate.comp" << std::endl;

        mComputeFFT = createComputeProgram(shaderManager, "lib/ocean/fft_compute.comp", defines);
        if (!mComputeFFT) std::cerr << "Failed to load lib/ocean/fft_compute.comp" << std::endl;

        mComputeTranspose = createComputeProgram(shaderManager, "lib/ocean/transpose.comp", defines);
        if (!mComputeTranspose) std::cerr << "Failed to load lib/ocean/transpose.comp" << std::endl;

        mComputeUnpack = createComputeProgram(shaderManager, "lib/ocean/fft_unpack.comp", defines);
        if (!mComputeUnpack) std::cerr << "Failed to load lib/ocean/fft_unpack.comp" << std::endl;
        
        std::cout << "Ocean::initShaders finished" << std::endl;
    }



    void Ocean::initBuffers()
    {
        const int numStages = getNumStages(L_SIZE);

        // Butterfly factor buffer: log2(map_size) x map_size x vec4
        const size_t butterflyElements = numStages * L_SIZE * 4;
        osg::ref_ptr<osg::FloatArray> butterflyData = new osg::FloatArray(butterflyElements);
        for (size_t i = 0; i < butterflyElements; ++i)
            (*butterflyData)[i] = 0.0f;

        mButterflyBuffer = new osg::VertexBufferObject;
        mButterflyBuffer->setDataVariance(osg::Object::STATIC);
        mButterflyBuffer->setUsage(GL_STATIC_DRAW);
        butterflyData->setBufferObject(mButterflyBuffer.get());

        // FFT working buffer: map_size x map_size x num_spectra x 2 * num_cascades x vec2
        // The "x 2" is for ping-pong buffering
        const size_t fftElements = L_SIZE * L_SIZE * NUM_SPECTRA * 2 * NUM_CASCADES * 2;
        osg::ref_ptr<osg::FloatArray> fftData = new osg::FloatArray(fftElements);
        for (size_t i = 0; i < fftElements; ++i)
            (*fftData)[i] = 0.0f;

        mFFTBuffer = new osg::VertexBufferObject;
        mFFTBuffer->setDataVariance(osg::Object::DYNAMIC);
        mFFTBuffer->setUsage(GL_DYNAMIC_DRAW);
        fftData->setBufferObject(mFFTBuffer.get());

        // Spectrum buffer for initial h0(k) generation
        const size_t spectrumElements = L_SIZE * L_SIZE * NUM_CASCADES * 4;
        osg::ref_ptr<osg::FloatArray> spectrumData = new osg::FloatArray(spectrumElements);
        for (size_t i = 0; i < spectrumElements; ++i)
            (*spectrumData)[i] = 0.0f;

        mSpectrumBuffer = new osg::VertexBufferObject;
        mSpectrumBuffer->setDataVariance(osg::Object::STATIC);
        mSpectrumBuffer->setUsage(GL_STATIC_DRAW);
        spectrumData->setBufferObject(mSpectrumBuffer.get());
    }

    void Ocean::initTextures()
    {
        // Create displacement map texture array (one layer per cascade)
        mDisplacementMap = new osg::Texture2DArray;
        mDisplacementMap->setTextureSize(L_SIZE, L_SIZE, NUM_CASCADES);
        mDisplacementMap->setInternalFormat(GL_RGBA16F);
        mDisplacementMap->setSourceFormat(GL_RGBA);
        mDisplacementMap->setSourceType(GL_HALF_FLOAT);
        mDisplacementMap->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        mDisplacementMap->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        mDisplacementMap->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        mDisplacementMap->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);

        // Initialize with zero displacement (flat water)
        size_t dispSize = L_SIZE * L_SIZE * NUM_CASCADES * 4 * sizeof(float);
        unsigned char* zeroData = new unsigned char[dispSize];
        std::memset(zeroData, 0, dispSize);
        osg::ref_ptr<osg::Image> dispImage = new osg::Image;
        dispImage->setImage(L_SIZE, L_SIZE, NUM_CASCADES, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT,
            zeroData, osg::Image::USE_NEW_DELETE);
        mDisplacementMap->setImage(0, dispImage);

        // Create normal map texture array
        mNormalMap = new osg::Texture2DArray;
        mNormalMap->setTextureSize(L_SIZE, L_SIZE, NUM_CASCADES);
        mNormalMap->setInternalFormat(GL_RGBA16F);
        mNormalMap->setSourceFormat(GL_RGBA);
        mNormalMap->setSourceType(GL_HALF_FLOAT);
        mNormalMap->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        mNormalMap->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        mNormalMap->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        mNormalMap->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);

        // Initialize with up-pointing normals (0, 0, 1, 0)
        size_t normalSize = L_SIZE * L_SIZE * NUM_CASCADES * 4 * sizeof(float);
        float* normalData = new float[L_SIZE * L_SIZE * NUM_CASCADES * 4];
        for (size_t i = 0; i < L_SIZE * L_SIZE * NUM_CASCADES; ++i)
        {
            normalData[i * 4] = 0.0f;     // x
            normalData[i * 4 + 1] = 0.0f; // y
            normalData[i * 4 + 2] = 1.0f; // z (up)
            normalData[i * 4 + 3] = 0.0f; // w (foam)
        }
        osg::ref_ptr<osg::Image> normalImage = new osg::Image;
        normalImage->setImage(L_SIZE, L_SIZE, NUM_CASCADES, GL_RGBA16F, GL_RGBA, GL_FLOAT,
            reinterpret_cast<unsigned char*>(normalData), osg::Image::USE_NEW_DELETE);
        mNormalMap->setImage(0, normalImage);

        // Create spectrum texture array (stores h0(k) and h0(-k)*)
        mSpectrum = new osg::Texture2DArray;
        mSpectrum->setTextureSize(L_SIZE, L_SIZE, NUM_CASCADES);
        mSpectrum->setInternalFormat(GL_RGBA16F);
        mSpectrum->setSourceFormat(GL_RGBA);
        mSpectrum->setSourceType(GL_HALF_FLOAT);
        mSpectrum->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        mSpectrum->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        mSpectrum->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        mSpectrum->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);

        // Initialize spectrum with zeros (will be filled by compute shader)
        size_t spectrumSize = L_SIZE * L_SIZE * NUM_CASCADES * 4 * sizeof(uint16_t);
        unsigned char* spectrumData = new unsigned char[spectrumSize];
        std::memset(spectrumData, 0, spectrumSize);
        osg::ref_ptr<osg::Image> spectrumImage = new osg::Image;
        spectrumImage->setImage(L_SIZE, L_SIZE, NUM_CASCADES, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT,
            spectrumData, osg::Image::USE_NEW_DELETE);
        mSpectrum->setImage(0, spectrumImage);
    }

    void Ocean::initializeComputePipeline()
    {
        // Create a dummy drawable for compute dispatch
        // This will be attached to the scene and its draw callback will dispatch compute shaders
        osg::ref_ptr<osg::Geometry> computeDispatcher = new osg::Geometry;
        computeDispatcher->setUseDisplayList(false);
        computeDispatcher->setUseVertexBufferObjects(false);

        // Empty vertex array (we're not actually drawing anything)
        computeDispatcher->setVertexArray(new osg::Vec3Array);
        
        // Prevent culling by setting a large bounding box
        computeDispatcher->setInitialBound(osg::BoundingBox(-1e9, -1e9, -1e9, 1e9, 1e9, 1e9));

        // Set the compute dispatch callback
        computeDispatcher->setDrawCallback(new ComputeDispatchCallback(this));

        // Add to the root node in a pre-render bin to ensure it runs before the water renders
        osg::ref_ptr<osg::Geode> computeGeode = new osg::Geode;
        computeGeode->addDrawable(computeDispatcher);
        computeGeode->getOrCreateStateSet()->setRenderBinDetails(-100, "RenderBin");
        computeGeode->setName("OceanComputeDispatcher");

        mRootNode->addChild(computeGeode);

        mInitialized = true;
    }

    // ...

    void Ocean::dispatchCompute(osg::State* state)
    {
        if (!state || !mInitialized)
            return;

        // Get OpenGL extensions for compute shaders
        osg::GLExtensions* ext = state->get<osg::GLExtensions>();
        if (!ext)
        {
            static bool warned = false;
            if (!warned) {
                 std::cerr << "Ocean::dispatchCompute: Failed to get GLExtensions" << std::endl;
                 warned = true;
            }
            return;
        }

        unsigned int contextID = state->getContextID();

        // For the first frame, run initialization compute shaders
        static bool sInitialized = false;
        if (!sInitialized)
        {
            std::cout << "Ocean::dispatchCompute: Running initialization shaders" << std::endl;
            initializeComputeShaders(state, ext, contextID);
            sInitialized = true;
        }

        // Every frame: run the simulation compute shaders
        updateComputeShaders(state, ext, contextID);
    }

    void Ocean::initializeComputeShaders(osg::State* state, osg::GLExtensions* ext, unsigned int contextID)
    {
        const int numStages = getNumStages(L_SIZE);

        // Get glBindBufferBase function pointer (not available in osg::GLExtensions in this version)
        typedef void (GL_APIENTRY * PFNGLBINDBUFFERBASEPROC) (GLenum target, GLuint index, GLuint buffer);
        static PFNGLBINDBUFFERBASEPROC glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)osg::getGLExtensionFuncPtr("glBindBufferBase");
        
        if (!glBindBufferBase)
        {
            // Try to get it again if failed (maybe context wasn't ready first time?)
            glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)osg::getGLExtensionFuncPtr("glBindBufferBase");
            if (!glBindBufferBase) {
                std::cerr << "Ocean::initializeComputeShaders: Failed to load glBindBufferBase extension!" << std::endl;
                return; 
            }
        }

        // Get glBindBuffer function pointer
        typedef void (GL_APIENTRY * PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
        static PFNGLBINDBUFFERPROC glBindBuffer = (PFNGLBINDBUFFERPROC)osg::getGLExtensionFuncPtr("glBindBuffer");
        
        // 1. Generate butterfly factors (once at startup)
        if (mComputeButterfly.valid())
        {
            state->applyAttribute(mComputeButterfly.get());

            // Bind butterfly buffer
            GLuint butterflyBufferID = mButterflyBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            
            // ALLOCATE butterfly buffer before initialization shader runs
            GLint bufSize = 0;
            ext->glBindBuffer(GL_SHADER_STORAGE_BUFFER, butterflyBufferID);
            ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);
            
            if (bufSize == 0) {
                std::cout << "Ocean: Allocating Butterfly Buffer..." << std::endl;
                const size_t butterflyElements = numStages * L_SIZE * 4;
                const size_t butterflySizeBytes = butterflyElements * sizeof(float);
                ext->glBufferData(GL_SHADER_STORAGE_BUFFER, butterflySizeBytes, NULL, GL_STATIC_DRAW);
                
                ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);
                std::cout << "Ocean: Allocated Butterfly Buffer. Size: " << bufSize << " bytes" << std::endl;
            }
            
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, butterflyBufferID);

            // Dispatch: work groups cover L_SIZE/128 x numStages x 1
            // (64 threads per group, 2 writes per thread = 128 columns per group)
            ext->glDispatchCompute(L_SIZE / 128, numStages, 1);
            ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // 2. Generate Initial Spectrum (once at startup)
        if (mComputeSpectrum.valid())
        {
            state->applyAttribute(mComputeSpectrum.get());
            const osg::Program::PerContextProgram* pcp = state->getLastAppliedProgramObject();

            // Bind Spectrum Texture (Image Unit 0, Write Only)
            osg::Texture::TextureObject* texObj = mSpectrum->getTextureObject(contextID);
            if (!texObj) { mSpectrum->apply(*state); texObj = mSpectrum->getTextureObject(contextID); }
            if (texObj) {
                GLuint spectrumID = texObj->id();
                ext->glBindImageTexture(0, spectrumID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            }

            // Per-cascade parameters matching Godot's diversity
            // Godot uses varied wind speeds and fetch lengths to create complex, sharp-crested waves
            // Godot cascades: 0=88m/10m/s/150km, 1=57m/5m/s/150km, 2=16m/20m/s/550km

            // Cascade tile sizes in METERS (compute shaders work in meters)
            // Matching Godot's pattern: large->medium->small for varied wave scales
            float tileSizesMeters[NUM_CASCADES] = { 88.0f, 57.0f, 16.0f, 16.0f };

            // Wind speeds per cascade (m/s) - varied for realistic wave complexity
            float windSpeeds[NUM_CASCADES] = { 10.0f, 5.0f, 20.0f, 20.0f };

            // Fetch lengths per cascade (meters) - how far wind has blown over water
            float fetchLengths[NUM_CASCADES] = { 150000.0f, 150000.0f, 550000.0f, 550000.0f };

            // Wind directions per cascade (radians) - slight variations for realism
            float windDirections[NUM_CASCADES] = {
                0.349f,  // 20 degrees
                0.262f,  // 15 degrees
                0.349f,  // 20 degrees
                0.349f   // 20 degrees
            };

            // Spread parameter per cascade - controls directional spreading
            float spreads[NUM_CASCADES] = { 0.2f, 0.4f, 0.4f, 0.4f };

            // Common parameters
            float depth = 20.0f; // meters (shallow water enhances wave steepness)
            float swell = 0.8f;
            float detail = 1.0f;

            const float G = 9.81f;

            for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
            {
                // Calculate JONSWAP parameters per cascade (wind/fetch specific)
                float alpha = 0.076f * std::pow(windSpeeds[cascade]*windSpeeds[cascade] / (fetchLengths[cascade]*G), 0.22f);
                float omega_p = 22.0f * std::pow(G*G / (windSpeeds[cascade]*fetchLengths[cascade]), 1.0f/3.0f);

                if (pcp)
                {
                    GLint loc;
                    // Pass tile_length in METERS to compute shaders
                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("tile_length"));
                    if (loc >= 0) ext->glUniform2f(loc, tileSizesMeters[cascade], tileSizesMeters[cascade]);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("cascade_index"));
                    if (loc >= 0) ext->glUniform1ui(loc, cascade);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("alpha"));
                    if (loc >= 0) ext->glUniform1f(loc, alpha);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("peak_frequency"));
                    if (loc >= 0) ext->glUniform1f(loc, omega_p);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("wind_speed"));
                    if (loc >= 0) ext->glUniform1f(loc, windSpeeds[cascade]);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("wind_direction"));
                    if (loc >= 0) ext->glUniform1f(loc, windDirections[cascade]);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("depth"));
                    if (loc >= 0) ext->glUniform1f(loc, depth);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("swell"));
                    if (loc >= 0) ext->glUniform1f(loc, swell);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("detail"));
                    if (loc >= 0) ext->glUniform1f(loc, detail);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("spread"));
                    if (loc >= 0) ext->glUniform1f(loc, spreads[cascade]);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("seed"));
                    if (loc >= 0) ext->glUniform2i(loc, cascade * 13 + 42, cascade * 17 + 99); // Different seed per cascade
                }

                // Dispatch (L_SIZE / 16, L_SIZE / 16, 1)
                ext->glDispatchCompute(L_SIZE / 16, L_SIZE / 16, 1);
            }
            ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }


    }

    void Ocean::updateComputeShaders(osg::State* state, osg::GLExtensions* ext, unsigned int contextID)
    {
        // Get glBindBufferBase function pointer (not available in osg::GLExtensions in this version)
        typedef void (GL_APIENTRY * PFNGLBINDBUFFERBASEPROC) (GLenum target, GLuint index, GLuint buffer);
        static PFNGLBINDBUFFERBASEPROC glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)osg::getGLExtensionFuncPtr("glBindBufferBase");
        
        if (!glBindBufferBase)
        {
            // Try to get it again if failed (maybe context wasn't ready first time?)
            glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)osg::getGLExtensionFuncPtr("glBindBufferBase");
            if (!glBindBufferBase) {
                 static bool warned = false;
                 if (!warned) {
                     std::cerr << "Ocean::updateComputeShaders: Failed to load glBindBufferBase extension!" << std::endl;
                     warned = true;
                 }
                 return;
            }
        }

        // Wave parameters in METERS (compute shaders use physical equations in meters)
        float depth = 1000.0f; // Deep water (1000 meters)

        // Cascade tile sizes in METERS (must match initializeComputeShaders)
        // IMPORTANT: Compute shaders work in meters, only convert to MW units in vertex shader
        float tileSizesMeters[NUM_CASCADES] = { 88.0f, 57.0f, 16.0f, 16.0f };

        // Per-cascade foam parameters matching Godot's main.tscn:
        // Cascade 0: whitecap=0.5, foam_amount=8.0
        // Cascade 1: whitecap=0.5, foam_amount=0.0 (no foam)
        // Cascade 2: whitecap=0.25, foam_amount=3.0
        // Cascade 3: whitecap=0.25, foam_amount=3.0 (duplicate)
        //
        // Godot formula: foam_grow_rate = delta * foam_amount * 7.5
        //                foam_decay_rate = delta * max(0.5, 10.0 - foam_amount) * 1.15
        // Assuming delta = 0.016s (60fps):
        float foamAmounts[NUM_CASCADES] = { 8.0f, 0.0f, 3.0f, 3.0f };
        float whitecaps[NUM_CASCADES] = { 0.5f, 0.5f, 0.25f, 0.25f };

        // Calculate foam rates per cascade (at 60fps)
        const float DELTA_TIME = 0.016f;
        float foamGrowRates[NUM_CASCADES];
        float foamDecayRates[NUM_CASCADES];
        for (int i = 0; i < NUM_CASCADES; ++i) {
            foamGrowRates[i] = DELTA_TIME * foamAmounts[i] * 7.5f;
            foamDecayRates[i] = DELTA_TIME * std::max(0.5f, 10.0f - foamAmounts[i]) * 1.15f;
        }

        // 1. Modulate Spectrum
        if (mComputeModulate.valid())
        {
            static int modFrame = 0;
            modFrame++;
            
            state->applyAttribute(mComputeModulate.get());
            const osg::Program::PerContextProgram* pcp = state->getLastAppliedProgramObject();
            
            // Bind FFT Buffer (Binding 1)
            GLuint fftBufferID_Local = mFFTBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            
            if (modFrame % 60 == 0) {
                std::cout << "Ocean: Modulate Frame " << modFrame << std::endl;
                std::cout << "  PCP Valid: " << (pcp ? "Yes" : "No") << std::endl;
                std::cout << "  FFT Buffer ID: " << fftBufferID_Local << std::endl;
                
                GLint bufSize = 0;
                ext->glBindBuffer(GL_SHADER_STORAGE_BUFFER, fftBufferID_Local);
                ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);
                
                if (bufSize == 0) {
                    std::cout << "Ocean: Allocating FFT Buffer..." << std::endl;
                    const size_t fftElements = L_SIZE * L_SIZE * NUM_SPECTRA * 2 * NUM_CASCADES * 2;
                    const size_t fftSizeBytes = fftElements * sizeof(float);
                    ext->glBufferData(GL_SHADER_STORAGE_BUFFER, fftSizeBytes, NULL, GL_DYNAMIC_DRAW);
                    
                    // Verify allocation
                    ext->glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, 0x8764, &bufSize);
                    std::cout << "Ocean: Allocated FFT Buffer. New Size: " << bufSize << " bytes" << std::endl;
                } else if (modFrame % 60 == 0) {
                     std::cout << "  FFT Buffer Size: " << bufSize << " bytes" << std::endl;
                }
            }

            // Bind Spectrum Texture (Image Unit 0)
            osg::Texture::TextureObject* texObj = mSpectrum->getTextureObject(contextID);
            if (texObj) {
                GLuint spectrumID = texObj->id();
                ext->glBindImageTexture(0, spectrumID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
            }
            
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftBufferID_Local);
            
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) std::cerr << "Ocean: Modulate glBindBufferBase(1) failed: " << err << " BufID: " << fftBufferID_Local << std::endl;
            
            for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
            {
                if (pcp)
                {
                    GLint loc;
                    // Pass tile_length in METERS to compute shaders
                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("tile_length"));
                    if (loc >= 0) ext->glUniform2f(loc, tileSizesMeters[cascade], tileSizesMeters[cascade]);

                    // Pass depth in METERS to compute shaders
                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("depth"));
                    if (loc >= 0) ext->glUniform1f(loc, depth);
                    
                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("time"));
                    if (loc >= 0) ext->glUniform1f(loc, mTime);
                    
                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("cascade_index"));
                    if (loc >= 0) ext->glUniform1ui(loc, cascade);
                }
                
                ext->glDispatchCompute(L_SIZE / 16, L_SIZE / 16, 1);
                
                if (modFrame % 60 == 0 && cascade == 0) {
                     GLenum dispatchErr = glGetError();
                     if (dispatchErr != GL_NO_ERROR) std::cerr << "Ocean: Modulate Dispatch Error: " << dispatchErr << std::endl;
                }
            }
            ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // 2. Horizontal FFT
        if (mComputeFFT.valid())
        {
            state->applyAttribute(mComputeFFT.get());
            const osg::Program::PerContextProgram* pcp = state->getLastAppliedProgramObject();
            
            // Bind Butterfly Buffer (Binding 0)
            GLuint butterflyBufferID = mButterflyBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, butterflyBufferID);
            
            // Bind FFT Buffer (Binding 1)
            GLuint fftBufferID = mFFTBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftBufferID);
            
            for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
            {
                if (pcp)
                {
                    GLint loc = pcp->getUniformLocation(osg::Uniform::getNameID("cascade_index"));
                    if (loc >= 0) ext->glUniform1ui(loc, cascade);
                }
                
                // Dispatch (1, L_SIZE, NUM_SPECTRA)
                ext->glDispatchCompute(1, L_SIZE, NUM_SPECTRA);
            }
            ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // 3. Transpose
        if (mComputeTranspose.valid())
        {
            state->applyAttribute(mComputeTranspose.get());
            const osg::Program::PerContextProgram* pcp = state->getLastAppliedProgramObject();
            
            // Bind FFT Buffer (Binding 1)
            GLuint fftBufferID = mFFTBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftBufferID);
            
            for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
            {
                if (pcp)
                {
                    GLint loc = pcp->getUniformLocation(osg::Uniform::getNameID("cascade_index"));
                    if (loc >= 0) ext->glUniform1ui(loc, cascade);
                }
                
                ext->glDispatchCompute(L_SIZE / 32, L_SIZE / 32, NUM_SPECTRA);
            }
            ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
            
        // 4. Vertical FFT (same as horizontal, but on transposed data)
        if (mComputeFFT.valid())
        {
            state->applyAttribute(mComputeFFT.get());
            const osg::Program::PerContextProgram* pcp = state->getLastAppliedProgramObject();
            
            // Bind Butterfly Buffer (Binding 0)
            GLuint butterflyBufferID = mButterflyBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, butterflyBufferID);
            
            // Bind FFT Buffer (Binding 1)
            GLuint fftBufferID = mFFTBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftBufferID);
            
            for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
            {
                if (pcp)
                {
                    GLint loc = pcp->getUniformLocation(osg::Uniform::getNameID("cascade_index"));
                    if (loc >= 0) ext->glUniform1ui(loc, cascade);
                }
                
                ext->glDispatchCompute(1, L_SIZE, NUM_SPECTRA);
            }
            ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
            
        // 5. Unpack to Textures
        if (mComputeUnpack.valid())
        {
            state->applyAttribute(mComputeUnpack.get());
            const osg::Program::PerContextProgram* pcp = state->getLastAppliedProgramObject();

            // Bind Displacement Map (Image Unit 0, Write Only)
            osg::Texture::TextureObject* dispTexObj = mDisplacementMap->getTextureObject(contextID);
            if (!dispTexObj) { mDisplacementMap->apply(*state); dispTexObj = mDisplacementMap->getTextureObject(contextID); }
            if (dispTexObj) {
                GLuint dispID = dispTexObj->id();
                ext->glBindImageTexture(0, dispID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            }
            
            // Bind Normal Map (Image Unit 1, Read/Write)
            osg::Texture::TextureObject* normTexObj = mNormalMap->getTextureObject(contextID);
            if (!normTexObj) { mNormalMap->apply(*state); normTexObj = mNormalMap->getTextureObject(contextID); }
            if (normTexObj) {
                GLuint normID = normTexObj->id();
                ext->glBindImageTexture(1, normID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
            }
            
            // Bind FFT Buffer (Binding 1)
            GLuint fftBufferID = mFFTBuffer->getOrCreateGLBufferObject(contextID)->getGLObjectID();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftBufferID);

            GLenum err2 = glGetError();
            if (err2 != GL_NO_ERROR) std::cerr << "Ocean: Unpack glBindBufferBase(1) failed: " << err2 << " BufID: " << fftBufferID << std::endl;
            
            for (int cascade = 0; cascade < NUM_CASCADES; ++cascade)
            {
                if (pcp)
                {
                    GLint loc;
                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("cascade_index"));
                    if (loc >= 0) ext->glUniform1ui(loc, cascade);

                    // Use per-cascade foam parameters
                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("whitecap"));
                    if (loc >= 0) ext->glUniform1f(loc, whitecaps[cascade]);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("foam_grow_rate"));
                    if (loc >= 0) ext->glUniform1f(loc, foamGrowRates[cascade]);

                    loc = pcp->getUniformLocation(osg::Uniform::getNameID("foam_decay_rate"));
                    if (loc >= 0) ext->glUniform1f(loc, foamDecayRates[cascade]);
                }

                ext->glDispatchCompute(L_SIZE / 16, L_SIZE / 16, 1);
            }
            ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        // Unbind resources to prevent state pollution
        ext->glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        ext->glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    }

    void Ocean::initGeometry()
    {
        // Create a large ocean plane with clipmap LOD system
        // Uses concentric rings of different resolutions for optimal detail near player
        mWaterGeom = new osg::Geometry;

        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);

        // Clipmap LOD Configuration
        // 5 rings aligned with FFT cascade tile sizes for optimal wave detail sampling
        // Cascade sizes: 50m, 100m, 200m, 400m (in MW units: 3,626, 7,253, 14,506, 29,012)
        struct LODRing {
            int gridSize;      // Number of grid cells per side
            float radius;      // Outer radius in MW units
            float innerRadius; // Inner radius (0 for center ring)
        };

        // Ring design aligned with cascade boundaries:
        // Ring 0 (ultra-fine): 512x512 grid, radius 1000 units  (~2.0 units/vertex)  - Wavelets visible!
        // Ring 1 (very fine):  256x256 grid, radius 1813 units  (~7.1 units/vertex)  - Cascade 0 (50m)
        // Ring 2 (fine):       128x128 grid, radius 3626 units  (~28.3 units/vertex) - Cascade 1 (100m)
        // Ring 3 (medium):     64x64 grid,   radius 7253 units  (~113 units/vertex)  - Cascade 2 (200m)
        // Ring 4 (coarse):     32x32 grid,   radius 14506 units (~453 units/vertex)  - Cascade 3 (400m)
        // Ring 5 (far):        32x32 grid,   radius 29012 units (~906 units/vertex)  - Cascade 3 (repeated)
        // Ring 6 (farther):    32x32 grid,   radius 58024 units (~1813 units/vertex) - Cascade 3 (repeated)
        // Ring 7 (horizon):    32x32 grid,   radius 116048 units (~3626 units/vertex) - Cascade 3 (repeated)
        // Ring 8 (distant):    32x32 grid,   radius 232096 units (~7253 units/vertex) - Cascade 3 (repeated)
        
        const float CASCADE_0_RADIUS = 50.0f * METERS_TO_MW_UNITS / 2.0f;   // Half of cascade 0 tile = 1,813 units
        const float CASCADE_1_RADIUS = 100.0f * METERS_TO_MW_UNITS / 2.0f;  // Half of cascade 1 tile = 3,626 units
        const float CASCADE_2_RADIUS = 200.0f * METERS_TO_MW_UNITS / 2.0f;  // Half of cascade 2 tile = 7,253 units
        const float CASCADE_3_RADIUS = 400.0f * METERS_TO_MW_UNITS / 2.0f;  // Half of cascade 3 tile = 14,506 units
        
        // Extended radii for horizon coverage
        const float RING_5_RADIUS = CASCADE_3_RADIUS * 2.0f; // 29,012
        const float RING_6_RADIUS = RING_5_RADIUS * 2.0f;    // 58,024
        const float RING_7_RADIUS = RING_6_RADIUS * 2.0f;    // 116,048 (~1.6km)
        const float RING_8_RADIUS = RING_7_RADIUS * 2.0f;    // 232,096 (~3.2km)
        const float RING_9_RADIUS = RING_8_RADIUS * 4.0f;    // 928,384 (~12.8km) - Horizon

        // CRITICAL: Grid snap size must divide evenly into all ring vertex spacings
        // to prevent texture swimming on outer rings when camera moves.
        // Ring 0 vertex spacing: (2 * CASCADE_0_RADIUS) / 512 = 7.08203125 units
        // All ring vertices MUST be positioned at multiples of this base spacing!
        const float BASE_GRID_SPACING = (2.0f * CASCADE_0_RADIUS) / 512.0f;

        LODRing rings[] = {
            { 512, CASCADE_0_RADIUS, 0.0f },              // Ring 0: Ultra-fine center (Matches Cascade 0 Texture Resolution)
            // Ring 1 removed (merged into Ring 0)
            { 128, CASCADE_1_RADIUS, CASCADE_0_RADIUS },  // Ring 1: Cascade 1
            { 64,  CASCADE_2_RADIUS, CASCADE_1_RADIUS },  // Ring 2: Cascade 2
            { 32,  CASCADE_3_RADIUS, CASCADE_2_RADIUS },  // Ring 3: Cascade 3
            { 32,  RING_5_RADIUS,    CASCADE_3_RADIUS },  // Ring 4: Extended
            { 32,  RING_6_RADIUS,    RING_5_RADIUS },     // Ring 5: Extended
            { 32,  RING_7_RADIUS,    RING_6_RADIUS },     // Ring 6: Extended
            { 32,  RING_8_RADIUS,    RING_7_RADIUS },     // Ring 7: Extended
            { 32,  RING_9_RADIUS,    RING_8_RADIUS }      // Ring 8: Horizon
        };

        const int numRings = 9;
        int vertexOffset = 0;

        // Generate each LOD ring
        for (int ringIdx = 0; ringIdx < numRings; ++ringIdx)
        {
            const LODRing& ring = rings[ringIdx];
            const int gridSize = ring.gridSize;
            const float outerRadius = ring.radius;
            const float innerRadius = ring.innerRadius;

            // For center ring (ringIdx == 0), create full square grid
            if (ringIdx == 0)
            {
                // Full grid from -radius to +radius
                const float step = (2.0f * outerRadius) / gridSize;

                for (int y = 0; y <= gridSize; ++y)
                {
                    for (int x = 0; x <= gridSize; ++x)
                    {
                        // Vertices are automatically aligned to BASE_GRID_SPACING
                        // because outerRadius and step are multiples of it
                        float px = -outerRadius + x * step;
                        float py = -outerRadius + y * step;
                        verts->push_back(osg::Vec3f(px, py, 0.f));
                    }
                }

                // Generate indices for full grid
                for (int y = 0; y < gridSize; ++y)
                {
                    for (int x = 0; x < gridSize; ++x)
                    {
                        int i0 = vertexOffset + y * (gridSize + 1) + x;
                        int i1 = i0 + 1;
                        int i2 = i0 + (gridSize + 1);
                        int i3 = i2 + 1;

                        indices->push_back(i0);
                        indices->push_back(i1);
                        indices->push_back(i2);

                        indices->push_back(i2);
                        indices->push_back(i1);
                        indices->push_back(i3);
                    }
                }

                vertexOffset += (gridSize + 1) * (gridSize + 1);
            }
            else
            {
                // Outer rings: create hollow square (donut shape)
                // Only generate vertices/faces in the area between innerRadius and outerRadius
                const float step = (2.0f * outerRadius) / gridSize;
                const int startVertex = vertexOffset;

                // Generate all grid points, snapping to BASE_GRID_SPACING to prevent texture swimming
                for (int y = 0; y <= gridSize; ++y)
                {
                    for (int x = 0; x <= gridSize; ++x)
                    {
                        float px = -outerRadius + x * step;
                        float py = -outerRadius + y * step;

                        // CRITICAL: Snap vertex positions to multiples of BASE_GRID_SPACING
                        // This ensures when camera snaps, all vertices land on consistent UV coordinates
                        px = std::round(px / BASE_GRID_SPACING) * BASE_GRID_SPACING;
                        py = std::round(py / BASE_GRID_SPACING) * BASE_GRID_SPACING;

                        verts->push_back(osg::Vec3f(px, py, 0.f));
                    }
                }

                // Generate indices, skipping quads that are inside innerRadius
                for (int y = 0; y < gridSize; ++y)
                {
                    for (int x = 0; x < gridSize; ++x)
                    {
                        // Check if this quad is outside the inner radius
                        float px = -outerRadius + (x + 0.5f) * step;
                        float py = -outerRadius + (y + 0.5f) * step;
                        float distFromCenter = std::sqrt(px*px + py*py);

                        // Only create triangles for quads outside inner ring
                        if (distFromCenter >= innerRadius)
                        {
                            int i0 = vertexOffset + y * (gridSize + 1) + x;
                            int i1 = i0 + 1;
                            int i2 = i0 + (gridSize + 1);
                            int i3 = i2 + 1;

                            indices->push_back(i0);
                            indices->push_back(i1);
                            indices->push_back(i2);

                            indices->push_back(i2);
                            indices->push_back(i1);
                            indices->push_back(i3);
                        }
                    }
                }

                vertexOffset += (gridSize + 1) * (gridSize + 1);
            }
        }

        mWaterGeom->setVertexArray(verts);
        mWaterGeom->addPrimitiveSet(indices);

        // Set up state set with our ocean shaders
        osg::StateSet* stateset = mWaterGeom->getOrCreateStateSet();

        // Load ocean shaders using the shader manager
        Shader::ShaderManager& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();
        Shader::ShaderManager::DefineMap defines;

        // Get the ocean program (vert + frag)
        osg::ref_ptr<osg::Program> program = new osg::Program;

        auto vert = shaderManager.getShader("ocean.vert", defines, osg::Shader::VERTEX);
        auto frag = shaderManager.getShader("ocean.frag", defines, osg::Shader::FRAGMENT);

        if (vert) program->addShader(vert);
        if (frag) program->addShader(frag);

        stateset->setAttributeAndModes(program, osg::StateAttribute::ON);

        // Bind textures
        stateset->setTextureAttributeAndModes(0, mDisplacementMap, osg::StateAttribute::ON);
        stateset->setTextureAttributeAndModes(1, mNormalMap, osg::StateAttribute::ON);

        // Set uniforms
        mNodePositionUniform = new osg::Uniform("nodePosition", osg::Vec3f(0,0,0));
        stateset->addUniform(mNodePositionUniform);
        mCameraPositionUniform = new osg::Uniform("cameraPosition", osg::Vec3f(0,0,0));
        stateset->addUniform(mCameraPositionUniform);
        stateset->addUniform(new osg::Uniform("displacementMap", 0));
        stateset->addUniform(new osg::Uniform("normalMap", 1));

        // Debug visualization uniform (0 = off, 1 = on)
        mDebugVisualizeCascadesUniform = new osg::Uniform("debugVisualizeCascades", 0);
        stateset->addUniform(mDebugVisualizeCascadesUniform);

        // Debug visualization for LOD density (0 = off, 1 = on)
        mDebugVisualizeLODUniform = new osg::Uniform("debugVisualizeLOD", 0);
        stateset->addUniform(mDebugVisualizeLODUniform);

        
        // DEBUG: Bind Spectrum for visualization
        // DEBUG: Bind Spectrum for visualization
        stateset->setTextureAttributeAndModes(2, mSpectrum, osg::StateAttribute::ON);
        stateset->addUniform(new osg::Uniform("spectrumMap", 2));
        stateset->addUniform(new osg::Uniform("numCascades", NUM_CASCADES));

        // Set cascade scales (each cascade covers a different area)
        // mapScales format: vec4(uvScale, uvScale, displacementScale, normalScale)
        // IMPORTANT: UV scale must match tile_length used in compute shaders
        osg::ref_ptr<osg::Uniform> mapScales = new osg::Uniform(osg::Uniform::FLOAT_VEC4, "mapScales", NUM_CASCADES);
        // Match the tile sizes from spectrum generation
        float tileSizesMeters[NUM_CASCADES] = { 88.0f, 57.0f, 16.0f, 16.0f };

        // Displacement and normal scales for each cascade
        // FFT outputs displacement in METERS (e.g., wave height of 1-3 meters for realistic ocean)
        // We need to convert to MW UNITS (×72.53) for vertex shader
        //
        // Matching Godot's approach from main.tscn:
        //   Cascade 0 (88m):  displacement_scale = 1.0,  normal_scale = 1.0
        //   Cascade 1 (57m):  displacement_scale = 0.75, normal_scale = 1.0
        //   Cascade 2 (16m):  displacement_scale = 0.0,  normal_scale = 0.25 (normals only!)
        //   Cascade 3 (16m):  displacement_scale = 0.0,  normal_scale = 0.25 (duplicate for foam)
        //
        // Key insight: Small tile cascades (16m) contribute ONLY normals/foam, not displacement
        // This creates fine surface detail without adding small bumps to geometry
        float displacementScales[NUM_CASCADES] = {
            1.0f * METERS_TO_MW_UNITS,   // Cascade 0 (88m): broad waves
            0.75f * METERS_TO_MW_UNITS,  // Cascade 1 (57m): medium waves
            0.0f,                         // Cascade 2 (16m): normals/foam only
            0.0f                          // Cascade 3 (16m): normals/foam only
        };
        float normalScales[NUM_CASCADES] = { 1.0f, 1.0f, 0.25f, 0.25f };

        for (int i = 0; i < NUM_CASCADES; ++i)
        {
            // UV scale in world space (Morrowind units)
            // World coordinates are in MW units, tile_length in compute shaders is in meters
            // So we need: uvScale = 1 / (tileSizeMeters * METERS_TO_MW_UNITS)
            float tileSizeMW = tileSizesMeters[i] * METERS_TO_MW_UNITS;
            float uvScale = 1.0f / tileSizeMW;
            mapScales->setElement(i, osg::Vec4f(uvScale, uvScale, displacementScales[i], normalScales[i]));
        }
        stateset->addUniform(mapScales);

        // Set RenderBin and Depth settings
        stateset->setRenderBinDetails(MWRender::RenderBin_Water, "RenderBin");
        stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
        
        // Ocean should be transparent and blend with the scene
        stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
        // Note: Removed TRANSPARENT_BIN hint to allow proper depth testing
        
        // Explicitly set blending function (Source Alpha, One Minus Source Alpha)
        osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc;
        blendFunc->setFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        stateset->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

        osg::ref_ptr<osg::Depth> depth = new osg::Depth;
        depth->setWriteMask(true);
        depth->setFunction(osg::Depth::GEQUAL); // Reverse-Z: Pass if Greater or Equal (Closer)
        stateset->setAttributeAndModes(depth, osg::StateAttribute::ON);

        // Sun parameters (would normally come from the scene)
        stateset->addUniform(new osg::Uniform("sunDir", osg::Vec3f(0.5f, 0.5f, 0.7f)));
        stateset->addUniform(new osg::Uniform("sunColor", osg::Vec3f(1.0f, 0.95f, 0.8f)));

        // Add to scene
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(mWaterGeom);

        mRootNode->addChild(geode);
    }

    void Ocean::setDebugVisualizeCascades(bool enabled)
    {
        if (mDebugVisualizeCascadesUniform)
            mDebugVisualizeCascadesUniform->set(enabled ? 1 : 0);
    }

    void Ocean::setDebugVisualizeLOD(bool enabled)
    {
        if (mDebugVisualizeLODUniform)
            mDebugVisualizeLODUniform->set(enabled ? 1 : 0);
    }

}
