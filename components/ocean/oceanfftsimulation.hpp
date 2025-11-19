#ifndef OPENMW_COMPONENTS_OCEAN_OCEANFFTSIMULATION_H
#define OPENMW_COMPONENTS_OCEAN_OCEANFFTSIMULATION_H

#include <osg/Texture2D>
#include <osg/Vec2f>
#include <osg/ref_ptr>
#include <osg/BufferObject>

#include <vector>
#include <memory>
#include <map>

#include "watertype.hpp"

namespace osg
{
    class Program;
    class Camera;
    class State;
    class BufferObject;
}

namespace Resource
{
    class ResourceSystem;
}

namespace Ocean
{
    /// FFT-based ocean wave simulation (GodotOceanWaves implementation)
    /// Implements physically-based ocean waves using Fast Fourier Transform
    /// Based on: https://github.com/2Retr0/GodotOceanWaves
    class OceanFFTSimulation
    {
    public:
        /// Wave cascade for multi-scale ocean simulation
        struct WaveCascade
        {
            float tileSize;              // World-space size of this cascade (meters)
            int textureResolution;        // Texture resolution (e.g., 256, 512, 1024)
            float updateInterval;         // Seconds between FFT updates
            float timeSinceUpdate;        // Accumulated time since last update

            // Output textures
            osg::ref_ptr<osg::Texture2D> spectrumTexture;     // H0(k) initial spectrum (rgba16f)
            osg::ref_ptr<osg::Texture2D> displacementTexture; // xyz displacement (rgba16f)
            osg::ref_ptr<osg::Texture2D> normalTexture;       // Normal + foam (rgba16f)

            // SSBO for FFT computation
            osg::ref_ptr<osg::BufferObject> fftBufferObject; // FFT working buffer (ping-pong)

            WaveCascade()
                : tileSize(100.0f)
                , textureResolution(512)
                , updateInterval(0.05f)
                , timeSinceUpdate(0.0f)
            {}
        };

        OceanFFTSimulation(Resource::ResourceSystem* resourceSystem);
        ~OceanFFTSimulation();

        /// Initialize the FFT simulation system
        /// @return true if initialization succeeded
        bool initialize();

        /// Dispatch compute shaders (called during rendering)
        /// @param state The current OpenGL state
        void dispatchCompute(osg::State* state);

        /// Update the simulation
        /// @param dt Delta time in seconds
        void update(float dt);

        /// Set wave parameters (JONSWAP/TMA spectrum)
        void setWindSpeed(float speed) { mWindSpeed = speed; mNeedsSpectrumRegeneration = true; }
        void setWindDirection(const osg::Vec2f& direction);
        void setWaterDepth(float depth) { mWaterDepth = depth; mNeedsSpectrumRegeneration = true; }
        void setFetchDistance(float distance) { mFetchDistance = distance; mNeedsSpectrumRegeneration = true; }

        // JONSWAP/TMA specific parameters
        void setAlpha(float alpha) { mAlpha = alpha; mNeedsSpectrumRegeneration = true; }
        void setSwell(float swell) { mSwell = swell; mNeedsSpectrumRegeneration = true; }
        void setDetail(float detail) { mDetail = detail; mNeedsSpectrumRegeneration = true; }
        void setSpread(float spread) { mSpread = spread; mNeedsSpectrumRegeneration = true; }

        // Foam parameters
        void setWhitecap(float whitecap) { mWhitecap = whitecap; }
        void setFoamGrowRate(float rate) { mFoamGrowRate = rate; }
        void setFoamDecayRate(float rate) { mFoamDecayRate = rate; }

        /// Get wave parameters
        float getWindSpeed() const { return mWindSpeed; }
        osg::Vec2f getWindDirection() const { return mWindDirection; }
        float getFetchDistance() const { return mFetchDistance; }
        float getWaterDepth() const { return mWaterDepth; }

        /// Get displacement texture for a cascade
        osg::Texture2D* getDisplacementTexture(int cascadeIndex) const;

        /// Get normal texture for a cascade
        osg::Texture2D* getNormalTexture(int cascadeIndex) const;

        /// Get foam texture for a cascade (foam is in alpha channel of normal texture)
        osg::Texture2D* getFoamTexture(int cascadeIndex) const;

        /// Get number of cascades
        int getCascadeCount() const { return static_cast<int>(mCascades.size()); }

        /// Get cascade tile size
        float getCascadeTileSize(int cascadeIndex) const;

        /// Check if FFT simulation is initialized
        bool isInitialized() const { return mInitialized; }

        /// Check if compute shaders are supported
        static bool supportsComputeShaders();

        /// Load ocean parameters from OceanParams struct
        void loadParameters(const OceanParams& params);

    private:
        /// Load compute shader programs (6 shaders for GodotOceanWaves)
        bool loadShaderPrograms();

        /// Initialize wave cascades
        void initializeCascades();

        /// Initialize a single cascade
        bool initializeCascade(WaveCascade& cascade);

        /// Create SSBOs for FFT computation
        bool createFFTBuffers(WaveCascade& cascade);

        /// Create butterfly factor buffer (shared across cascades)
        bool createButterflyBuffer(int resolution);

        /// Generate initial spectrum using spectrum_compute shader
        void generateSpectrum(osg::State* state, WaveCascade& cascade);

        /// Create a floating-point texture
        osg::ref_ptr<osg::Texture2D> createFloatTexture(int width, int height, GLenum internalFormat);

        /// Helper: Set uniform values for a program
        void setUniform(osg::Program* program, const char* name, float value);
        void setUniform(osg::Program* program, const char* name, const osg::Vec2f& value);
        void setUniformInt(osg::Program* program, const char* name, int value);
        void setUniformInt(osg::Program* program, const char* name, int x, int y);
        void setUniformUInt(osg::Program* program, const char* name, unsigned int value);

        /// Helper: Bind SSBO to binding point
        void bindSSBO(osg::BufferObject* buffer, unsigned int binding);

        /// Helper: Bind image texture for compute shader access
        void bindImage(osg::State* state, osg::Texture2D* texture, GLuint index, GLenum access);

        /// Wave parameters (JONSWAP/TMA spectrum)
        float mWindSpeed;           // Wind speed in m/s
        osg::Vec2f mWindDirection;  // Normalized wind direction
        float mWindAngle;           // Wind angle in radians
        float mFetchDistance;       // Fetch distance in meters
        float mWaterDepth;          // Water depth in meters (for TMA spectrum)

        // JONSWAP/TMA parameters
        float mAlpha;               // Spectral amplitude (typically 0.0081)
        float mPeakFrequency;       // Peak frequency (calculated from wind speed)
        float mSwell;               // Swell parameter (0.0 - 1.0)
        float mDetail;              // Detail level (0.0 - 1.0)
        float mSpread;              // Directional spreading (0.0 - 1.0)

        // Foam parameters
        float mWhitecap;            // Jacobian threshold for foam generation
        float mFoamGrowRate;        // Foam growth rate
        float mFoamDecayRate;       // Foam decay rate

        /// Cascades for multi-scale waves
        std::vector<WaveCascade> mCascades;

        /// Butterfly factor buffer (shared across cascades of same resolution)
        std::map<int, osg::ref_ptr<osg::BufferObject>> mButterflyBuffers;

        /// Shader programs (6 shaders for GodotOceanWaves pipeline)
        osg::ref_ptr<osg::Program> mSpectrumComputeProgram;      // spectrum_compute.comp
        osg::ref_ptr<osg::Program> mSpectrumModulateProgram;     // spectrum_modulate.comp
        osg::ref_ptr<osg::Program> mButterflyFactorsProgram;     // fft_butterfly_factors.comp
        osg::ref_ptr<osg::Program> mFFTStockhamProgram;          // fft_stockham.comp
        osg::ref_ptr<osg::Program> mFFTTransposeProgram;         // fft_transpose.comp
        osg::ref_ptr<osg::Program> mFFTUnpackProgram;            // fft_unpack.comp

        /// Global simulation time
        float mSimulationTime;

        /// Whether the simulation is initialized
        bool mInitialized;

        /// Whether spectrum needs regeneration (parameter change)
        bool mNeedsSpectrumRegeneration;

        /// Resource system for shader loading
        Resource::ResourceSystem* mResourceSystem;

        /// Performance preset (determines cascade count and resolution)
        enum class PerformancePreset
        {
            LOW,    // 2 cascades, 256x256
            MEDIUM, // 2 cascades, 512x512
            HIGH,   // 3 cascades, 512x512
            ULTRA   // 3 cascades, 1024x1024
        };

        PerformancePreset mPreset;

        /// Get preset configuration
        struct PresetConfig
        {
            int cascadeCount;
            int resolution;
            float updateInterval;
        };

        PresetConfig getPresetConfig() const;

        /// Random seed for spectrum generation
        osg::Vec2i mRandomSeed;
    };
}

#endif
