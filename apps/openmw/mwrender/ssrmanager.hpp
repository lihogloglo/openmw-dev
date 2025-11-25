#ifndef OPENMW_MWRENDER_SSRMANAGER_HPP
#define OPENMW_MWRENDER_SSRMANAGER_HPP

#include <osg/Camera>
#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Vec3f>
#include <osg/ref_ptr>

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    /**
     * @brief Screen-Space Reflections Manager for water surfaces
     *
     * Implements efficient SSR using raymarch technique in screen space.
     * Provides high-quality local reflections with automatic fallback handling.
     *
     * Features:
     * - Adaptive step size for performance
     * - Binary search refinement for accuracy
     * - Confidence mask for blend control
     * - Screen-edge fading
     */
    class SSRManager
    {
    public:
        /**
         * @brief SSR quality settings
         */
        struct Params
        {
            float maxDistance = 100.0f;       ///< Maximum raymarch distance in world units
            int maxSteps = 128;               ///< Maximum raymarch iterations
            float stepSize = 1.0f;            ///< Base step size multiplier
            int binarySearchSteps = 4;        ///< Refinement iterations after hit
            float thickness = 0.5f;           ///< Depth buffer thickness for hit detection
            float fadeStart = 0.85f;          ///< Screen-edge fade start (0-1 from center)
            float fadeEnd = 0.95f;            ///< Screen-edge fade end (full fade at edges)
            float fresnelExponent = 3.0f;     ///< Fresnel falloff power
            bool enabled = true;              ///< Master enable/disable
        };

        SSRManager(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~SSRManager();

        /**
         * @brief Initialize SSR render targets and shaders
         * @param width Render target width
         * @param height Render target height
         */
        void initialize(int width, int height);

        /**
         * @brief Update SSR parameters
         */
        void setParams(const Params& params);
        const Params& getParams() const { return mParams; }

        /**
         * @brief Set input textures from scene rendering
         * @param colorBuffer Scene color buffer
         * @param depthBuffer Scene depth buffer
         * @param normalBuffer Scene normal buffer (optional, can be nullptr)
         */
        void setInputTextures(osg::Texture2D* colorBuffer, osg::Texture2D* depthBuffer,
            osg::Texture2D* normalBuffer = nullptr);

        /**
         * @brief Get SSR result texture
         * @return RGBA texture (RGB = reflection color, A = confidence 0-1)
         */
        osg::Texture2D* getResultTexture() const { return mSSRTexture.get(); }

        /**
         * @brief Get SSR camera for adding to render graph
         */
        osg::Camera* getCamera() const { return mSSRCamera.get(); }

        /**
         * @brief Enable or disable SSR
         */
        void setEnabled(bool enabled);
        bool isEnabled() const { return mParams.enabled; }

        /**
         * @brief Update SSR rendering
         * @param viewMatrix Current view matrix
         * @param projectionMatrix Current projection matrix
         */
        void update(const osg::Matrix& viewMatrix, const osg::Matrix& projectionMatrix);

        /**
         * @brief Resize render targets
         */
        void resize(int width, int height);

    private:
        void createRenderTargets(int width, int height);
        void createSSRCamera();
        void createSSRShader();
        void updateUniforms();

        osg::ref_ptr<osg::Group> mParent;
        Resource::ResourceSystem* mResourceSystem;

        // Render targets
        osg::ref_ptr<osg::Camera> mSSRCamera;
        osg::ref_ptr<osg::Texture2D> mSSRTexture; ///< Output: RGB=reflection, A=confidence
        osg::ref_ptr<osg::Geometry> mFullscreenQuad;

        // Input textures
        osg::ref_ptr<osg::Texture2D> mColorBuffer;
        osg::ref_ptr<osg::Texture2D> mDepthBuffer;
        osg::ref_ptr<osg::Texture2D> mNormalBuffer;

        // Uniforms
        osg::ref_ptr<osg::Uniform> mViewMatrixUniform;
        osg::ref_ptr<osg::Uniform> mProjectionMatrixUniform;
        osg::ref_ptr<osg::Uniform> mInvViewProjectionUniform;
        osg::ref_ptr<osg::Uniform> mMaxDistanceUniform;
        osg::ref_ptr<osg::Uniform> mMaxStepsUniform;
        osg::ref_ptr<osg::Uniform> mStepSizeUniform;
        osg::ref_ptr<osg::Uniform> mThicknessUniform;
        osg::ref_ptr<osg::Uniform> mFadeParamsUniform;

        Params mParams;
        int mWidth;
        int mHeight;
    };
}

#endif
