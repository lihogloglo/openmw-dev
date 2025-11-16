#ifndef OPENMW_COMPONENTS_OCEAN_OCEANFFTSIMULATION_H
#define OPENMW_COMPONENTS_OCEAN_OCEANFFTSIMULATION_H

#include <osg/Texture2D>
#include <osg/Vec2f>
#include <osg/ref_ptr>

#include <vector>
#include <memory>

#include "watertype.hpp"

namespace osg
{
    class Program;
    class Camera;
}

namespace Ocean
{
    /// FFT-based ocean wave simulation (GodotOceanWaves style)
    /// Implements physically-based ocean waves using Fast Fourier Transform
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

            // FFT textures
            osg::ref_ptr<osg::Texture2D> spectrumTexture;     // H(k) initial spectrum
            osg::ref_ptr<osg::Texture2D> displacementTexture; // xyz displacement
            osg::ref_ptr<osg::Texture2D> normalTexture;       // Normal vectors
            osg::ref_ptr<osg::Texture2D> foamTexture;         // Jacobian-based foam

            // FFT working textures (ping-pong buffers)
            osg::ref_ptr<osg::Texture2D> fftTemp1;
            osg::ref_ptr<osg::Texture2D> fftTemp2;

            WaveCascade()
                : tileSize(100.0f)
                , textureResolution(512)
                , updateInterval(0.05f)
                , timeSinceUpdate(0.0f)
            {}
        };

        OceanFFTSimulation();
        ~OceanFFTSimulation();

        /// Initialize the FFT simulation system
        /// @return true if initialization succeeded
        bool initialize();

        /// Update the simulation
        /// @param dt Delta time in seconds
        void update(float dt);

        /// Set wave parameters
        void setWindSpeed(float speed) { mWindSpeed = speed; }
        void setWindDirection(const osg::Vec2f& direction);
        void setFetchDistance(float distance) { mFetchDistance = distance; }
        void setWaterDepth(float depth) { mWaterDepth = depth; }
        void setChoppiness(float choppiness) { mChoppiness = choppiness; }

        /// Get wave parameters
        float getWindSpeed() const { return mWindSpeed; }
        osg::Vec2f getWindDirection() const { return mWindDirection; }
        float getFetchDistance() const { return mFetchDistance; }
        float getWaterDepth() const { return mWaterDepth; }
        float getChoppiness() const { return mChoppiness; }

        /// Get displacement texture for a cascade
        osg::Texture2D* getDisplacementTexture(int cascadeIndex) const;

        /// Get normal texture for a cascade
        osg::Texture2D* getNormalTexture(int cascadeIndex) const;

        /// Get foam texture for a cascade
        osg::Texture2D* getFoamTexture(int cascadeIndex) const;

        /// Get number of cascades
        int getCascadeCount() const { return static_cast<int>(mCascades.size()); }

        /// Get cascade tile size
        float getCascadeTileSize(int cascadeIndex) const;

        /// Check if compute shaders are supported
        static bool supportsComputeShaders();

        /// Load ocean parameters from OceanParams struct
        void loadParameters(const OceanParams& params);

    private:
        /// Initialize wave cascades
        void initializeCascades();

        /// Initialize a single cascade
        bool initializeCascade(WaveCascade& cascade);

        /// Generate initial spectrum texture (Phillips/JONSWAP)
        void generateSpectrum(WaveCascade& cascade);

        /// Update a cascade using FFT
        void updateCascade(WaveCascade& cascade, float time);

        /// Perform 2D FFT on a texture (horizontal + vertical passes)
        void performFFT2D(WaveCascade& cascade);

        /// Generate displacement and normal maps from spectrum
        void generateDisplacementAndNormals(WaveCascade& cascade);

        /// Create butterfly texture for FFT (twiddle factors)
        osg::ref_ptr<osg::Texture2D> createButterflyTexture(int resolution);

        /// Create a floating-point texture
        osg::ref_ptr<osg::Texture2D> createFloatTexture(int width, int height, int components);

        /// Wave parameters
        float mWindSpeed;           // Wind speed in m/s
        osg::Vec2f mWindDirection;  // Normalized wind direction
        float mFetchDistance;       // Fetch distance in meters
        float mWaterDepth;          // Water depth in meters
        float mChoppiness;          // Wave steepness multiplier

        /// Cascades for multi-scale waves
        std::vector<WaveCascade> mCascades;

        /// Butterfly texture (shared across cascades)
        std::map<int, osg::ref_ptr<osg::Texture2D>> mButterflyTextures;

        /// Shader programs
        osg::ref_ptr<osg::Program> mSpectrumGeneratorProgram;
        osg::ref_ptr<osg::Program> mFFTHorizontalProgram;
        osg::ref_ptr<osg::Program> mFFTVerticalProgram;
        osg::ref_ptr<osg::Program> mDisplacementProgram;

        /// Global simulation time
        float mSimulationTime;

        /// Whether the simulation is initialized
        bool mInitialized;

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
    };
}

#endif
