#include "oceanfftsimulation.hpp"

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/Program>
#include <osg/State>
#include <osg/StateSet>
#include <osg/BufferObject>
#include <osg/Array>
#include <osg/GLExtensions>
#include <osg/Uniform>
#include <osg/Vec2i>

#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/glextensions.hpp>
#include <components/shader/shadermanager.hpp>

namespace Ocean
{
    namespace
    {
        constexpr float GRAVITY = 9.81f; // m/s^2
        constexpr float PI = 3.14159265359f;
        constexpr unsigned int NUM_SPECTRA = 4u; // Number of packed wave spectra
    }

    OceanFFTSimulation::OceanFFTSimulation(Resource::ResourceSystem* resourceSystem)
        : mResourceSystem(resourceSystem)
        , mWindSpeed(15.0f)
        , mWindDirection(1.0f, 0.0f)
        , mWindAngle(0.0f)
        , mFetchDistance(100000.0f)
        , mWaterDepth(1000.0f)
        , mAlpha(0.0081f)
        , mPeakFrequency(0.855f * GRAVITY / 15.0f) // Calculated from wind speed
        , mSwell(0.3f)
        , mDetail(0.8f)
        , mSpread(0.5f)
        , mWhitecap(0.5f)
        , mFoamGrowRate(0.3f)
        , mFoamDecayRate(0.1f)
        , mSimulationTime(0.0f)
        , mInitialized(false)
        , mNeedsSpectrumRegeneration(true)
        , mPreset(PerformancePreset::HIGH)
        , mRandomSeed(42, 17)
    {
    }

    OceanFFTSimulation::~OceanFFTSimulation()
    {
    }

    bool OceanFFTSimulation::initialize()
    {
        if (mInitialized)
            return true;

        Log(Debug::Info) << "[OCEAN FFT] Initializing GodotOceanWaves pipeline...";

        // Check for compute shader support
        if (!supportsComputeShaders())
        {
            Log(Debug::Warning) << "[OCEAN FFT] Compute shaders not supported, ocean FFT simulation disabled";
            return false;
        }

        // Load shader programs (6 shaders for GodotOceanWaves)
        if (!loadShaderPrograms())
        {
            Log(Debug::Error) << "[OCEAN FFT] Failed to load ocean FFT shaders";
            return false;
        }

        // Initialize cascades based on performance preset
        initializeCascades();

        mInitialized = true;
        Log(Debug::Info) << "[OCEAN FFT] Initialized with " << mCascades.size() << " cascades";

        return true;
    }

    bool OceanFFTSimulation::loadShaderPrograms()
    {
        if (!mResourceSystem)
        {
            Log(Debug::Error) << "[OCEAN FFT] No resource system available for loading shaders";
            return false;
        }

        auto& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();

        Log(Debug::Info) << "[OCEAN FFT] Loading 6 compute shaders for GodotOceanWaves pipeline...";

        // Load the 6 compute shaders for GodotOceanWaves
        osg::ref_ptr<osg::Shader> spectrumComputeShader =
            shaderManager.getShader("core/ocean/spectrum_compute.comp", {}, osg::Shader::COMPUTE);
        osg::ref_ptr<osg::Shader> spectrumModulateShader =
            shaderManager.getShader("core/ocean/spectrum_modulate.comp", {}, osg::Shader::COMPUTE);
        osg::ref_ptr<osg::Shader> butterflyFactorsShader =
            shaderManager.getShader("core/ocean/fft_butterfly_factors.comp", {}, osg::Shader::COMPUTE);
        osg::ref_ptr<osg::Shader> fftStockhamShader =
            shaderManager.getShader("core/ocean/fft_stockham.comp", {}, osg::Shader::COMPUTE);
        osg::ref_ptr<osg::Shader> fftTransposeShader =
            shaderManager.getShader("core/ocean/fft_transpose.comp", {}, osg::Shader::COMPUTE);
        osg::ref_ptr<osg::Shader> fftUnpackShader =
            shaderManager.getShader("core/ocean/fft_unpack.comp", {}, osg::Shader::COMPUTE);

        // Check all shaders loaded successfully
        auto checkShaderSource = [](osg::Shader* shader, const char* name) {
            if (!shader)
            {
                Log(Debug::Error) << "[OCEAN FFT] Shader " << name << " is null";
                return false;
            }
            std::string source = shader->getShaderSource();
            if (source.empty())
            {
                Log(Debug::Error) << "[OCEAN FFT] Shader " << name << " has empty source";
                return false;
            }
            Log(Debug::Info) << "[OCEAN FFT] Loaded " << name << " (" << source.length() << " bytes)";
            return true;
        };

        if (!checkShaderSource(spectrumComputeShader.get(), "spectrum_compute.comp") ||
            !checkShaderSource(spectrumModulateShader.get(), "spectrum_modulate.comp") ||
            !checkShaderSource(butterflyFactorsShader.get(), "fft_butterfly_factors.comp") ||
            !checkShaderSource(fftStockhamShader.get(), "fft_stockham.comp") ||
            !checkShaderSource(fftTransposeShader.get(), "fft_transpose.comp") ||
            !checkShaderSource(fftUnpackShader.get(), "fft_unpack.comp"))
        {
            Log(Debug::Error) << "[OCEAN FFT] Failed to load one or more compute shaders";
            return false;
        }

        // Create shader programs
        mSpectrumComputeProgram = shaderManager.getComputeProgram(spectrumComputeShader);
        mSpectrumModulateProgram = shaderManager.getComputeProgram(spectrumModulateShader);
        mButterflyFactorsProgram = shaderManager.getComputeProgram(butterflyFactorsShader);
        mFFTStockhamProgram = shaderManager.getComputeProgram(fftStockhamShader);
        mFFTTransposeProgram = shaderManager.getComputeProgram(fftTransposeShader);
        mFFTUnpackProgram = shaderManager.getComputeProgram(fftUnpackShader);

        if (!mSpectrumComputeProgram || !mSpectrumModulateProgram ||
            !mButterflyFactorsProgram || !mFFTStockhamProgram ||
            !mFFTTransposeProgram || !mFFTUnpackProgram)
        {
            Log(Debug::Error) << "[OCEAN FFT] Failed to create one or more shader programs";
            return false;
        }

        Log(Debug::Info) << "[OCEAN FFT] Successfully loaded all 6 compute shader programs";
        return true;
    }

    void OceanFFTSimulation::initializeCascades()
    {
        PresetConfig config = getPresetConfig();

        mCascades.clear();
        mCascades.resize(config.cascadeCount);

        // Configure cascades with increasing tile sizes
        // Each cascade covers 4x the area of the previous one
        float baseSize = 50.0f; // 50 meters for finest cascade

        for (int i = 0; i < config.cascadeCount; ++i)
        {
            WaveCascade& cascade = mCascades[i];
            cascade.tileSize = baseSize * std::pow(4.0f, static_cast<float>(i));
            cascade.textureResolution = config.resolution;
            cascade.updateInterval = config.updateInterval;
            cascade.timeSinceUpdate = 0.0f;

            if (!initializeCascade(cascade))
            {
                Log(Debug::Error) << "[OCEAN FFT] Failed to initialize cascade " << i;
                mCascades.clear();
                return;
            }

            Log(Debug::Info) << "[OCEAN FFT] Cascade " << i << ": tile size = " << cascade.tileSize
                           << "m, resolution = " << cascade.textureResolution;
        }
    }

    bool OceanFFTSimulation::initializeCascade(WaveCascade& cascade)
    {
        int res = cascade.textureResolution;

        // Create output textures
        cascade.spectrumTexture = createFloatTexture(res, res, GL_RGBA16F_ARB);
        cascade.displacementTexture = createFloatTexture(res, res, GL_RGBA16F_ARB);
        cascade.normalTexture = createFloatTexture(res, res, GL_RGBA16F_ARB);

        if (!cascade.spectrumTexture || !cascade.displacementTexture || !cascade.normalTexture)
        {
            Log(Debug::Error) << "[OCEAN FFT] Failed to create output textures";
            return false;
        }

        // Create FFT buffers (SSBOs)
        if (!createFFTBuffers(cascade))
        {
            Log(Debug::Error) << "[OCEAN FFT] Failed to create FFT buffers";
            return false;
        }

        // Create butterfly buffer if not already created for this resolution
        if (mButterflyBuffers.find(res) == mButterflyBuffers.end())
        {
            if (!createButterflyBuffer(res))
            {
                Log(Debug::Error) << "[OCEAN FFT] Failed to create butterfly buffer";
                return false;
            }
        }

        return true;
    }

    bool OceanFFTSimulation::createFFTBuffers(WaveCascade& cascade)
    {
        int res = cascade.textureResolution;

        // FFT buffer size: map_size × map_size × num_spectra × 2 (ping-pong) × sizeof(vec2)
        // Each element is vec2 (complex number), so 2 floats = 8 bytes
        size_t fftBufferSize = static_cast<size_t>(res) * res * NUM_SPECTRA * 2 * 2 * sizeof(float);

        // Allocate buffer data (initialize to zero)
        std::vector<float> zeroData(res * res * NUM_SPECTRA * 2 * 2, 0.0f);
        osg::ref_ptr<osg::FloatArray> bufferData = new osg::FloatArray(zeroData.begin(), zeroData.end());

        cascade.fftBufferObject = new osg::ShaderStorageBufferObject;
        cascade.fftBufferObject->setUsage(GL_DYNAMIC_DRAW);
        bufferData->setBufferObject(cascade.fftBufferObject.get());

        Log(Debug::Info) << "[OCEAN FFT] Created FFT buffer: " << (fftBufferSize / 1024.0f / 1024.0f) << " MB";

        return true;
    }

    bool OceanFFTSimulation::createButterflyBuffer(int resolution)
    {
        // Butterfly buffer size: log2(resolution) × resolution × sizeof(vec4)
        int stages = static_cast<int>(std::log2(resolution));
        size_t butterflyBufferSize = static_cast<size_t>(stages) * resolution * 4 * sizeof(float);

        // Allocate buffer data
        std::vector<float> zeroData(stages * resolution * 4, 0.0f);
        osg::ref_ptr<osg::FloatArray> bufferData = new osg::FloatArray(zeroData.begin(), zeroData.end());

        osg::ref_ptr<osg::ShaderStorageBufferObject> butterflyBuffer = new osg::ShaderStorageBufferObject;
        butterflyBuffer->setUsage(GL_STATIC_DRAW);
        bufferData->setBufferObject(butterflyBuffer.get());

        mButterflyBuffers[resolution] = butterflyBuffer;

        Log(Debug::Info) << "[OCEAN FFT] Created butterfly buffer for resolution " << resolution
                       << ": " << (butterflyBufferSize / 1024.0f) << " KB";

        return true;
    }

    void OceanFFTSimulation::generateSpectrum(osg::State* state, WaveCascade& cascade)
    {
        if (!state || !mSpectrumComputeProgram)
            return;

        osg::GLExtensions* ext = state->get<osg::GLExtensions>();
        if (!ext)
            return;

        // Create a temporary state set for uniforms
        osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
        // OSG doesn't have Vec2i uniform support, so we'll use array or separate uniforms
        // For now, convert to Vec2f which shaders can handle
        stateset->addUniform(new osg::Uniform("uSeed", osg::Vec2f(static_cast<float>(mRandomSeed.x()), static_cast<float>(mRandomSeed.y()))));
        stateset->addUniform(new osg::Uniform("uTileLength", osg::Vec2f(cascade.tileSize, cascade.tileSize)));
        stateset->addUniform(new osg::Uniform("uAlpha", mAlpha));
        stateset->addUniform(new osg::Uniform("uPeakFrequency", mPeakFrequency));
        stateset->addUniform(new osg::Uniform("uWindSpeed", mWindSpeed));
        stateset->addUniform(new osg::Uniform("uAngle", mWindAngle));
        stateset->addUniform(new osg::Uniform("uDepth", mWaterDepth));
        stateset->addUniform(new osg::Uniform("uSwell", mSwell));
        stateset->addUniform(new osg::Uniform("uDetail", mDetail));
        stateset->addUniform(new osg::Uniform("uSpread", mSpread));

        // Apply state set and program
        state->pushStateSet(stateset);
        state->apply();
        state->applyAttribute(mSpectrumComputeProgram.get());

        // Apply uniforms to the program
        for (const auto& [name, stack] : state->getUniformMap())
        {
            if (!stack.uniformVec.empty())
                state->getLastAppliedProgramObject()->apply(*(stack.uniformVec.back().first));
        }

        // Bind output spectrum texture
        bindImage(state, cascade.spectrumTexture.get(), 0, GL_WRITE_ONLY_ARB);

        // Dispatch compute shader (16x16 local size)
        int res = cascade.textureResolution;
        ext->glDispatchCompute((res + 15) / 16, (res + 15) / 16, 1);
        ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        state->popStateSet();

        Log(Debug::Info) << "[OCEAN FFT] Generated initial spectrum for cascade (tile=" << cascade.tileSize << "m)";
    }

    void OceanFFTSimulation::dispatchCompute(osg::State* state)
    {
        if (!mInitialized || !state)
            return;

        osg::GLExtensions* ext = state->get<osg::GLExtensions>();
        if (!ext)
            return;

        const unsigned int contextID = static_cast<unsigned int>(state->getContextID());

        static bool firstDispatch = true;
        static bool shaderCompileFailed = false;

        // If shaders failed to compile, disable FFT and return immediately
        if (shaderCompileFailed)
        {
            if (firstDispatch)
            {
                Log(Debug::Warning) << "[OCEAN FFT] Shaders failed to compile, disabling FFT ocean";
                mInitialized = false;
                firstDispatch = false;
            }
            return;
        }

        if (firstDispatch)
        {
            Log(Debug::Info) << "[OCEAN FFT] First compute dispatch - validating shaders...";

            // Validate shader programs compiled successfully
            auto checkShader = [&](osg::Program* program, const char* name) {
                if (!program)
                {
                    Log(Debug::Error) << "[OCEAN FFT] Program '" << name << "' is null";
                    return false;
                }

                // Try to compile the program if not already compiled
                program->compileGLObjects(*state);

                // Get individual shader logs
                const osg::Program::ShaderList& shaders = program->getShaders();
                for (unsigned int i = 0; i < shaders.size(); ++i)
                {
                    osg::Shader* shader = shaders[i].get();
                    std::string shaderInfoLog;
                    if (shader && shader->getGlShaderInfoLog(contextID, shaderInfoLog) && !shaderInfoLog.empty())
                    {
                        Log(Debug::Error) << "[OCEAN FFT] Shader '" << name << "' info log:\n" << shaderInfoLog;
                    }
                }

                // Check program link status
                osg::Program::PerContextProgram* pcp = program->getPCP(*state);
                if (!pcp)
                {
                    Log(Debug::Error) << "[OCEAN FFT] Shader program '" << name << "' failed to compile/link";
                    std::string programInfoLog;
                    if (program->getGlProgramInfoLog(contextID, programInfoLog) && !programInfoLog.empty())
                    {
                        Log(Debug::Error) << "[OCEAN FFT] Program link log:\n" << programInfoLog;
                    }
                    return false;
                }
                return true;
            };

            bool allShadersValid = true;
            allShadersValid &= checkShader(mSpectrumComputeProgram.get(), "spectrum_compute");
            allShadersValid &= checkShader(mSpectrumModulateProgram.get(), "spectrum_modulate");
            allShadersValid &= checkShader(mButterflyFactorsProgram.get(), "fft_butterfly_factors");
            allShadersValid &= checkShader(mFFTStockhamProgram.get(), "fft_stockham");
            allShadersValid &= checkShader(mFFTTransposeProgram.get(), "fft_transpose");
            allShadersValid &= checkShader(mFFTUnpackProgram.get(), "fft_unpack");

            if (!allShadersValid)
            {
                Log(Debug::Error) << "[OCEAN FFT] One or more compute shader programs failed to compile - disabling FFT ocean";
                shaderCompileFailed = true;
                mInitialized = false;
                firstDispatch = false;
                return;
            }

            Log(Debug::Info) << "[OCEAN FFT] All compute shaders compiled successfully";

            // Generate butterfly factors for each resolution (one-time initialization)
            for (auto& pair : mButterflyBuffers)
            {
                int resolution = pair.first;
                osg::BufferObject* butterflyBuffer = pair.second.get();

                // Create state set for uniforms
                osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
                stateset->addUniform(new osg::Uniform("uMapSize", static_cast<unsigned int>(resolution)));

                state->pushStateSet(stateset);
                state->apply();
                state->applyAttribute(mButterflyFactorsProgram.get());

                // Apply uniforms
                for (const auto& [name, stack] : state->getUniformMap())
                {
                    if (!stack.uniformVec.empty())
                        state->getLastAppliedProgramObject()->apply(*(stack.uniformVec.back().first));
                }

                // Bind butterfly buffer to SSBO binding point 0
                osg::GLBufferObject* glBufferObject = butterflyBuffer->getOrCreateGLBufferObject(contextID);
                if (glBufferObject)
                {
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, glBufferObject->getGLObjectID());
                }

                // Dispatch (64x1 local size, log2(resolution) rows)
                int stages = static_cast<int>(std::log2(resolution));
                ext->glDispatchCompute((resolution + 63) / 64, stages, 1);
                ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                state->popStateSet();

                Log(Debug::Info) << "[OCEAN FFT] Generated butterfly factors for resolution " << resolution;
            }

            // Generate initial spectrum for each cascade (one-time initialization)
            for (auto& cascade : mCascades)
            {
                generateSpectrum(state, cascade);
            }

            mNeedsSpectrumRegeneration = false;
            firstDispatch = false;
        }

        // Regenerate spectrum if parameters changed
        if (mNeedsSpectrumRegeneration)
        {
            for (auto& cascade : mCascades)
            {
                generateSpectrum(state, cascade);
            }
            mNeedsSpectrumRegeneration = false;
        }

        // Process each cascade
        for (auto& cascade : mCascades)
        {
            int res = cascade.textureResolution;

            // Get butterfly buffer for this resolution
            auto butterflyIt = mButterflyBuffers.find(res);
            if (butterflyIt == mButterflyBuffers.end())
                continue;
            osg::BufferObject* butterflyBuffer = butterflyIt->second.get();

            // 1. Spectrum Modulate - time evolution and gradient calculation
            {
                osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
                stateset->addUniform(new osg::Uniform("uTileLength", osg::Vec2f(cascade.tileSize, cascade.tileSize)));
                stateset->addUniform(new osg::Uniform("uDepth", mWaterDepth));
                stateset->addUniform(new osg::Uniform("uTime", mSimulationTime));
                stateset->addUniform(new osg::Uniform("uMapSize", static_cast<unsigned int>(res)));

                state->pushStateSet(stateset);
                state->apply();
                state->applyAttribute(mSpectrumModulateProgram.get());

                for (const auto& [name, stack] : state->getUniformMap())
                {
                    if (!stack.uniformVec.empty())
                        state->getLastAppliedProgramObject()->apply(*(stack.uniformVec.back().first));
                }

                bindImage(state, cascade.spectrumTexture.get(), 0, GL_READ_ONLY_ARB);

                osg::GLBufferObject* fftGLBuffer = cascade.fftBufferObject->getOrCreateGLBufferObject(contextID);
                if (fftGLBuffer)
                {
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftGLBuffer->getGLObjectID());
                }

                ext->glDispatchCompute((res + 15) / 16, (res + 15) / 16, 1);
                ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                state->popStateSet();
            }

            // 2. Horizontal FFT passes (4 times, one per spectrum)
            for (unsigned int spectrumIdx = 0; spectrumIdx < NUM_SPECTRA; ++spectrumIdx)
            {
                osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
                stateset->addUniform(new osg::Uniform("uMapSize", static_cast<unsigned int>(res)));
                stateset->addUniform(new osg::Uniform("uSpectrumIndex", spectrumIdx));

                state->pushStateSet(stateset);
                state->apply();
                state->applyAttribute(mFFTStockhamProgram.get());

                for (const auto& [name, stack] : state->getUniformMap())
                {
                    if (!stack.uniformVec.empty())
                        state->getLastAppliedProgramObject()->apply(*(stack.uniformVec.back().first));
                }

                osg::GLBufferObject* butterflyGLBuffer = butterflyBuffer->getOrCreateGLBufferObject(contextID);
                osg::GLBufferObject* fftGLBuffer = cascade.fftBufferObject->getOrCreateGLBufferObject(contextID);
                if (butterflyGLBuffer && fftGLBuffer)
                {
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, butterflyGLBuffer->getGLObjectID());
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftGLBuffer->getGLObjectID());
                }

                // Dispatch (local_size_x = MAX_MAP_SIZE = 1024, one row per invocation)
                ext->glDispatchCompute(1, res, 1);
                ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                state->popStateSet();
            }

            // 3. Transpose (4 times, one per spectrum)
            for (unsigned int spectrumIdx = 0; spectrumIdx < NUM_SPECTRA; ++spectrumIdx)
            {
                osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
                stateset->addUniform(new osg::Uniform("uMapSize", static_cast<unsigned int>(res)));
                stateset->addUniform(new osg::Uniform("uSpectrumIndex", spectrumIdx));

                state->pushStateSet(stateset);
                state->apply();
                state->applyAttribute(mFFTTransposeProgram.get());

                for (const auto& [name, stack] : state->getUniformMap())
                {
                    if (!stack.uniformVec.empty())
                        state->getLastAppliedProgramObject()->apply(*(stack.uniformVec.back().first));
                }

                osg::GLBufferObject* butterflyGLBuffer = butterflyBuffer->getOrCreateGLBufferObject(contextID);
                osg::GLBufferObject* fftGLBuffer = cascade.fftBufferObject->getOrCreateGLBufferObject(contextID);
                if (butterflyGLBuffer && fftGLBuffer)
                {
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, butterflyGLBuffer->getGLObjectID());
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftGLBuffer->getGLObjectID());
                }

                // Dispatch (32x32 local size for tiling)
                ext->glDispatchCompute((res + 31) / 32, (res + 31) / 32, 1);
                ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                state->popStateSet();
            }

            // 4. Vertical FFT passes (4 times, one per spectrum)
            for (unsigned int spectrumIdx = 0; spectrumIdx < NUM_SPECTRA; ++spectrumIdx)
            {
                osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
                stateset->addUniform(new osg::Uniform("uMapSize", static_cast<unsigned int>(res)));
                stateset->addUniform(new osg::Uniform("uSpectrumIndex", spectrumIdx));

                state->pushStateSet(stateset);
                state->apply();
                state->applyAttribute(mFFTStockhamProgram.get());

                for (const auto& [name, stack] : state->getUniformMap())
                {
                    if (!stack.uniformVec.empty())
                        state->getLastAppliedProgramObject()->apply(*(stack.uniformVec.back().first));
                }

                osg::GLBufferObject* butterflyGLBuffer = butterflyBuffer->getOrCreateGLBufferObject(contextID);
                osg::GLBufferObject* fftGLBuffer = cascade.fftBufferObject->getOrCreateGLBufferObject(contextID);
                if (butterflyGLBuffer && fftGLBuffer)
                {
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, butterflyGLBuffer->getGLObjectID());
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, fftGLBuffer->getGLObjectID());
                }

                ext->glDispatchCompute(1, res, 1);
                ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                state->popStateSet();
            }

            // 5. Unpack - generate displacement and normal maps with foam
            {
                osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
                stateset->addUniform(new osg::Uniform("uMapSize", static_cast<unsigned int>(res)));
                stateset->addUniform(new osg::Uniform("uWhitecap", mWhitecap));
                stateset->addUniform(new osg::Uniform("uFoamGrowRate", mFoamGrowRate));
                stateset->addUniform(new osg::Uniform("uFoamDecayRate", mFoamDecayRate));

                state->pushStateSet(stateset);
                state->apply();
                state->applyAttribute(mFFTUnpackProgram.get());

                for (const auto& [name, stack] : state->getUniformMap())
                {
                    if (!stack.uniformVec.empty())
                        state->getLastAppliedProgramObject()->apply(*(stack.uniformVec.back().first));
                }

                bindImage(state, cascade.displacementTexture.get(), 0, GL_WRITE_ONLY_ARB);
                bindImage(state, cascade.normalTexture.get(), 1, GL_READ_WRITE_ARB);

                osg::GLBufferObject* fftGLBuffer = cascade.fftBufferObject->getOrCreateGLBufferObject(contextID);
                if (fftGLBuffer)
                {
                    ext->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, fftGLBuffer->getGLObjectID());
                }

                // Dispatch (16x16x2 local size)
                ext->glDispatchCompute((res + 15) / 16, (res + 15) / 16, 2);
                ext->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

                state->popStateSet();
            }
        }
    }

    void OceanFFTSimulation::update(float dt)
    {
        if (!mInitialized)
            return;

        mSimulationTime += dt;

        // Update cascades timing
        for (auto& cascade : mCascades)
        {
            cascade.timeSinceUpdate += dt;
            if (cascade.timeSinceUpdate >= cascade.updateInterval)
            {
                cascade.timeSinceUpdate = 0.0f;
            }
        }
    }

    void OceanFFTSimulation::setWindDirection(const osg::Vec2f& direction)
    {
        mWindDirection = direction;
        mWindDirection.normalize();

        // Calculate wind angle
        mWindAngle = std::atan2(mWindDirection.y(), mWindDirection.x());

        mNeedsSpectrumRegeneration = true;
    }

    osg::Texture2D* OceanFFTSimulation::getDisplacementTexture(int cascadeIndex) const
    {
        if (cascadeIndex >= 0 && cascadeIndex < static_cast<int>(mCascades.size()))
            return mCascades[cascadeIndex].displacementTexture.get();
        return nullptr;
    }

    osg::Texture2D* OceanFFTSimulation::getNormalTexture(int cascadeIndex) const
    {
        if (cascadeIndex >= 0 && cascadeIndex < static_cast<int>(mCascades.size()))
            return mCascades[cascadeIndex].normalTexture.get();
        return nullptr;
    }

    osg::Texture2D* OceanFFTSimulation::getFoamTexture(int cascadeIndex) const
    {
        // Foam is stored in the alpha channel of the normal texture
        if (cascadeIndex >= 0 && cascadeIndex < static_cast<int>(mCascades.size()))
            return mCascades[cascadeIndex].normalTexture.get();
        return nullptr;
    }

    float OceanFFTSimulation::getCascadeTileSize(int cascadeIndex) const
    {
        if (cascadeIndex >= 0 && cascadeIndex < static_cast<int>(mCascades.size()))
            return mCascades[cascadeIndex].tileSize;
        return 0.0f;
    }

    bool OceanFFTSimulation::supportsComputeShaders()
    {
        // Check for OpenGL 4.3+ (required for compute shaders)
#ifdef __APPLE__
        // Apple platform compute shader support is unreliable
        return false;
#else
        constexpr float minimumGLVersionRequiredForCompute = 4.3f;
        osg::GLExtensions& exts = SceneUtil::getGLExtensions();
        return exts.glVersion >= minimumGLVersionRequiredForCompute
            && exts.glslLanguageVersion >= minimumGLVersionRequiredForCompute;
#endif
    }

    void OceanFFTSimulation::loadParameters(const OceanParams& params)
    {
        mWindSpeed = params.windSpeed;
        mWindDirection = osg::Vec2f(params.windDirectionX, params.windDirectionY);
        mWindDirection.normalize();
        mWindAngle = std::atan2(mWindDirection.y(), mWindDirection.x());
        mFetchDistance = params.fetchDistance;
        mWaterDepth = params.waterDepth;

        // Calculate peak frequency from wind speed
        mPeakFrequency = 0.855f * GRAVITY / mWindSpeed;

        mNeedsSpectrumRegeneration = true;
    }

    osg::ref_ptr<osg::Texture2D> OceanFFTSimulation::createFloatTexture(int width, int height, GLenum internalFormat)
    {
        GLenum format;
        if (internalFormat == GL_RGBA16F_ARB)
            format = GL_RGBA;
        else if (internalFormat == GL_RGB16F_ARB)
            format = GL_RGB;
        else if (internalFormat == GL_RG16F)
            format = GL_RG;
        else
            format = GL_RED;

        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(width, height, 1, format, GL_FLOAT);

        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(image);
        texture->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::LINEAR);
        texture->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::LINEAR);
        texture->setWrap(osg::Texture2D::WRAP_S, osg::Texture2D::REPEAT);
        texture->setWrap(osg::Texture2D::WRAP_T, osg::Texture2D::REPEAT);
        texture->setInternalFormat(internalFormat);

        return texture;
    }

    void OceanFFTSimulation::setUniform(osg::Program* program, const char* name, float value)
    {
        // This is a placeholder - uniforms should be set via StateSet
        // For now, we'll rely on the state stack approach used in dispatchCompute
    }

    void OceanFFTSimulation::setUniform(osg::Program* program, const char* name, const osg::Vec2f& value)
    {
        // This is a placeholder - uniforms should be set via StateSet
        // For now, we'll rely on the state stack approach used in dispatchCompute
    }

    void OceanFFTSimulation::setUniformInt(osg::Program* program, const char* name, int x, int y)
    {
        // This is a placeholder - uniforms should be set via StateSet
        // For now, we'll rely on the state stack approach used in dispatchCompute
    }

    void OceanFFTSimulation::setUniformInt(osg::Program* program, const char* name, int value)
    {
        // This is a placeholder - uniforms should be set via StateSet
        // For now, we'll rely on the state stack approach used in dispatchCompute
    }

    void OceanFFTSimulation::setUniformUInt(osg::Program* program, const char* name, unsigned int value)
    {
        // This is a placeholder - uniforms should be set via StateSet
        // For now, we'll rely on the state stack approach used in dispatchCompute
    }

    void OceanFFTSimulation::bindSSBO(osg::BufferObject* buffer, unsigned int binding)
    {
        // This function is now unused - SSBOs are bound in dispatchCompute using proper GL extensions
        // Keeping it as a stub for now
    }

    void OceanFFTSimulation::bindImage(osg::State* state, osg::Texture2D* texture, GLuint index, GLenum access)
    {
        if (!state || !texture)
            return;

        osg::GLExtensions* ext = state->get<osg::GLExtensions>();
        if (!ext)
            return;

        const unsigned int contextID = static_cast<unsigned int>(state->getContextID());

        // Ensure texture object is created
        osg::Texture::TextureObject* to = texture->getTextureObject(contextID);
        if (!to || texture->isDirty(contextID))
        {
            state->applyTextureAttribute(index, texture);
            to = texture->getTextureObject(contextID);
        }

        if (to)
        {
            ext->glBindImageTexture(index, to->id(), 0, GL_FALSE, 0, access, texture->getInternalFormat());
        }
    }

    OceanFFTSimulation::PresetConfig OceanFFTSimulation::getPresetConfig() const
    {
        PresetConfig config;

        switch (mPreset)
        {
            case PerformancePreset::LOW:
                config.cascadeCount = 2;
                config.resolution = 256;
                config.updateInterval = 0.1f;
                break;

            case PerformancePreset::MEDIUM:
                config.cascadeCount = 2;
                config.resolution = 512;
                config.updateInterval = 0.05f;
                break;

            case PerformancePreset::HIGH:
                config.cascadeCount = 3;
                config.resolution = 512;
                config.updateInterval = 0.05f;
                break;

            case PerformancePreset::ULTRA:
                config.cascadeCount = 3;
                config.resolution = 1024;
                config.updateInterval = 0.033f;
                break;
        }

        return config;
    }
}
