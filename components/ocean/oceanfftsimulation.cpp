#include "oceanfftsimulation.hpp"

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/Program>

#include <cmath>
#include <complex>
#include <random>

#include <components/debug/debuglog.hpp>

namespace Ocean
{
    namespace
    {
        constexpr float GRAVITY = 9.81f; // m/s^2
        constexpr float PI = 3.14159265359f;

        /// Phillips spectrum for initial wave spectrum
        float phillipsSpectrum(const osg::Vec2f& k, float windSpeed, const osg::Vec2f& windDir)
        {
            float kLength = k.length();
            if (kLength < 0.0001f)
                return 0.0f;

            float kLength2 = kLength * kLength;
            float kLength4 = kLength2 * kLength2;

            // Wind speed at 10m height
            float V = windSpeed;
            float V2 = V * V;

            // Largest possible wave from this wind
            float L = V2 / GRAVITY;
            float L2 = L * L;

            // Damping for small waves
            float l = L / 1000.0f;
            float l2 = l * l;

            // Direction alignment
            osg::Vec2f kNorm = k / kLength;
            float kdotw = kNorm * windDir;
            float kdotw2 = kdotw * kdotw;

            // Phillips spectrum
            float phillips = std::exp(-1.0f / (kLength2 * L2)) / kLength4;
            phillips *= std::exp(-kLength2 * l2); // Damping
            phillips *= kdotw2; // Directional

            return phillips;
        }

        /// Generate random Gaussian number (Box-Muller transform)
        std::complex<float> gaussianRandom(std::mt19937& gen)
        {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            float u1 = dist(gen);
            float u2 = dist(gen);

            float r = std::sqrt(-2.0f * std::log(u1));
            float theta = 2.0f * PI * u2;

            return std::complex<float>(r * std::cos(theta), r * std::sin(theta));
        }
    }

    OceanFFTSimulation::OceanFFTSimulation()
        : mWindSpeed(10.0f)
        , mWindDirection(1.0f, 0.0f)
        , mFetchDistance(100000.0f)
        , mWaterDepth(1000.0f)
        , mChoppiness(1.5f)
        , mSimulationTime(0.0f)
        , mInitialized(false)
        , mPreset(PerformancePreset::MEDIUM)
    {
    }

    OceanFFTSimulation::~OceanFFTSimulation()
    {
    }

    bool OceanFFTSimulation::initialize()
    {
        if (mInitialized)
            return true;

        Log(Debug::Info) << "Initializing OceanFFTSimulation...";

        // Check for compute shader support
        if (!supportsComputeShaders())
        {
            Log(Debug::Warning) << "Compute shaders not supported, ocean FFT simulation disabled";
            return false;
        }

        // Initialize cascades based on performance preset
        initializeCascades();

        mInitialized = true;
        Log(Debug::Info) << "OceanFFTSimulation initialized with " << mCascades.size() << " cascades";

        return true;
    }

    void OceanFFTSimulation::update(float dt)
    {
        if (!mInitialized)
            return;

        mSimulationTime += dt;

        // Update cascades (staggered to distribute load)
        for (auto& cascade : mCascades)
        {
            cascade.timeSinceUpdate += dt;

            if (cascade.timeSinceUpdate >= cascade.updateInterval)
            {
                updateCascade(cascade, mSimulationTime);
                cascade.timeSinceUpdate = 0.0f;
                break; // Only update one cascade per frame for performance
            }
        }
    }

    void OceanFFTSimulation::setWindDirection(const osg::Vec2f& direction)
    {
        mWindDirection = direction;
        mWindDirection.normalize();

        // Regenerate spectrum when wind direction changes
        if (mInitialized)
        {
            for (auto& cascade : mCascades)
            {
                generateSpectrum(cascade);
            }
        }
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
        if (cascadeIndex >= 0 && cascadeIndex < static_cast<int>(mCascades.size()))
            return mCascades[cascadeIndex].foamTexture.get();
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
        // TODO: Proper OpenGL capability check
        // For now, assume compute shaders are available (OpenGL 4.3+)
        return true;
    }

    void OceanFFTSimulation::loadParameters(const OceanParams& params)
    {
        mWindSpeed = params.windSpeed;
        mWindDirection = osg::Vec2f(params.windDirectionX, params.windDirectionY);
        mWindDirection.normalize();
        mFetchDistance = params.fetchDistance;
        mWaterDepth = params.waterDepth;
        mChoppiness = params.choppiness;

        // Regenerate spectrum with new parameters
        if (mInitialized)
        {
            for (auto& cascade : mCascades)
            {
                generateSpectrum(cascade);
            }
        }
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
                Log(Debug::Error) << "Failed to initialize cascade " << i;
                mCascades.clear();
                return;
            }

            Log(Debug::Info) << "Cascade " << i << ": tile size = " << cascade.tileSize
                           << "m, resolution = " << cascade.textureResolution;
        }
    }

    bool OceanFFTSimulation::initializeCascade(WaveCascade& cascade)
    {
        int res = cascade.textureResolution;

        // Create textures
        cascade.spectrumTexture = createFloatTexture(res, res, 4);
        cascade.displacementTexture = createFloatTexture(res, res, 4);
        cascade.normalTexture = createFloatTexture(res, res, 4);
        cascade.foamTexture = createFloatTexture(res, res, 1);
        cascade.fftTemp1 = createFloatTexture(res, res, 4);
        cascade.fftTemp2 = createFloatTexture(res, res, 4);

        if (!cascade.spectrumTexture || !cascade.displacementTexture ||
            !cascade.normalTexture || !cascade.foamTexture)
        {
            return false;
        }

        // Generate initial spectrum
        generateSpectrum(cascade);

        // Create butterfly texture if not already created for this resolution
        if (mButterflyTextures.find(res) == mButterflyTextures.end())
        {
            mButterflyTextures[res] = createButterflyTexture(res);
        }

        return true;
    }

    void OceanFFTSimulation::generateSpectrum(WaveCascade& cascade)
    {
        int res = cascade.textureResolution;
        osg::Image* image = cascade.spectrumTexture->getImage();

        if (!image)
        {
            image = new osg::Image;
            image->allocateImage(res, res, 1, GL_RGBA, GL_FLOAT);
            cascade.spectrumTexture->setImage(image);
        }

        float* data = reinterpret_cast<float*>(image->data());

        // Random number generator for spectrum
        std::mt19937 gen(42); // Fixed seed for reproducibility

        // Generate spectrum using Phillips spectrum
        float L = cascade.tileSize;
        float kScale = 2.0f * PI / L;

        for (int y = 0; y < res; ++y)
        {
            for (int x = 0; x < res; ++x)
            {
                // Wave vector k
                float kx = (x - res / 2.0f) * kScale;
                float ky = (y - res / 2.0f) * kScale;
                osg::Vec2f k(kx, ky);

                // Phillips spectrum
                float P = phillipsSpectrum(k, mWindSpeed, mWindDirection);

                // Random Gaussian samples
                std::complex<float> h0 = gaussianRandom(gen) * std::sqrt(P * 0.5f);
                std::complex<float> h0conj = std::conj(gaussianRandom(gen) * std::sqrt(P * 0.5f));

                // Store in texture (real, imag, realConj, imagConj)
                int idx = (y * res + x) * 4;
                data[idx + 0] = h0.real();
                data[idx + 1] = h0.imag();
                data[idx + 2] = h0conj.real();
                data[idx + 3] = h0conj.imag();
            }
        }

        image->dirty();
    }

    void OceanFFTSimulation::updateCascade(WaveCascade& cascade, float time)
    {
        // TODO: Implement FFT-based update using compute shaders
        // For now, this is a placeholder

        // The full implementation would:
        // 1. Update spectrum based on time (dispersion relation)
        // 2. Perform horizontal FFT pass
        // 3. Transpose
        // 4. Perform vertical FFT pass
        // 5. Generate displacement and normal maps from frequency domain

        // This requires compute shaders which we'll implement next
    }

    void OceanFFTSimulation::performFFT2D(WaveCascade& cascade)
    {
        // TODO: Implement 2D FFT using compute shaders
        // This will use the Stockham FFT algorithm
    }

    void OceanFFTSimulation::generateDisplacementAndNormals(WaveCascade& cascade)
    {
        // TODO: Generate displacement (xyz) and normal maps from spectrum
        // Also compute Jacobian for foam generation
    }

    osg::ref_ptr<osg::Texture2D> OceanFFTSimulation::createButterflyTexture(int resolution)
    {
        // Create butterfly texture for FFT twiddle factors
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(resolution, static_cast<int>(std::log2(resolution)) + 1, 1, GL_RGBA, GL_FLOAT);

        float* data = reinterpret_cast<float*>(image->data());
        int stages = static_cast<int>(std::log2(resolution));

        for (int stage = 0; stage <= stages; ++stage)
        {
            for (int i = 0; i < resolution; ++i)
            {
                int butterflySpan = 1 << stage;
                int butterflyWidth = butterflySpan << 1;

                int butterflyIndex = i & (butterflySpan - 1);
                int twiddleIndex = butterflyIndex * resolution / butterflyWidth;

                // Twiddle factor: e^(-2πi * k/N)
                float angle = -2.0f * PI * static_cast<float>(twiddleIndex) / static_cast<float>(resolution);
                float twiddleReal = std::cos(angle);
                float twiddleImag = std::sin(angle);

                int topWing = (i / butterflyWidth) * butterflyWidth + butterflyIndex;
                int bottomWing = topWing + butterflySpan;

                int idx = (stage * resolution + i) * 4;
                data[idx + 0] = twiddleReal;
                data[idx + 1] = twiddleImag;
                data[idx + 2] = static_cast<float>(topWing);
                data[idx + 3] = static_cast<float>(bottomWing);
            }
        }

        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(image);
        texture->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::NEAREST);
        texture->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::NEAREST);
        texture->setWrap(osg::Texture2D::WRAP_S, osg::Texture2D::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture2D::WRAP_T, osg::Texture2D::CLAMP_TO_EDGE);

        return texture;
    }

    osg::ref_ptr<osg::Texture2D> OceanFFTSimulation::createFloatTexture(int width, int height, int components)
    {
        GLenum format;
        switch (components)
        {
            case 1: format = GL_RED; break;
            case 2: format = GL_RG; break;
            case 3: format = GL_RGB; break;
            case 4: format = GL_RGBA; break;
            default: return nullptr;
        }

        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(width, height, 1, format, GL_FLOAT);

        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(image);
        texture->setFilter(osg::Texture2D::MIN_FILTER, osg::Texture2D::LINEAR);
        texture->setFilter(osg::Texture2D::MAG_FILTER, osg::Texture2D::LINEAR);
        texture->setWrap(osg::Texture2D::WRAP_S, osg::Texture2D::REPEAT);
        texture->setWrap(osg::Texture2D::WRAP_T, osg::Texture2D::REPEAT);
        texture->setInternalFormat(format == GL_RGBA ? GL_RGBA32F_ARB :
                                   format == GL_RGB ? GL_RGB32F_ARB :
                                   format == GL_RG ? GL_RG32F :
                                   GL_R32F);

        return texture;
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
