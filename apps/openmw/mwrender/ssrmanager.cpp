#include "ssrmanager.hpp"

#include <osg/Geometry>
#include <osg/Material>
#include <osg/PositionAttitudeTransform>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/Uniform>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/shader/shadermanager.hpp>

#include "vismask.hpp"

namespace MWRender
{
    SSRManager::SSRManager(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
        : mParent(parent)
        , mResourceSystem(resourceSystem)
        , mWidth(0)
        , mHeight(0)
    {
    }

    SSRManager::~SSRManager()
    {
        if (mSSRCamera && mParent)
            mParent->removeChild(mSSRCamera);
    }

    void SSRManager::initialize(int width, int height)
    {
        mWidth = width;
        mHeight = height;

        createRenderTargets(width, height);
        createSSRCamera();
        createSSRShader();
        updateUniforms();

        if (mParent)
            mParent->addChild(mSSRCamera);
    }

    void SSRManager::createRenderTargets(int width, int height)
    {
        // Create SSR output texture (RGBA: RGB = reflection color, A = confidence)
        mSSRTexture = new osg::Texture2D;
        mSSRTexture->setTextureSize(width, height);
        mSSRTexture->setInternalFormat(GL_RGBA16F_ARB);
        mSSRTexture->setSourceFormat(GL_RGBA);
        mSSRTexture->setSourceType(GL_FLOAT);
        mSSRTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        mSSRTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        mSSRTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        mSSRTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    }

    void SSRManager::createSSRCamera()
    {
        mSSRCamera = new osg::Camera;
        mSSRCamera->setName("SSR Camera");
        mSSRCamera->setRenderOrder(osg::Camera::PRE_RENDER);
        mSSRCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
        mSSRCamera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
        mSSRCamera->setClearMask(GL_COLOR_BUFFER_BIT);
        mSSRCamera->setClearColor(osg::Vec4(0.0f, 0.0f, 0.0f, 0.0f));
        mSSRCamera->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
        mSSRCamera->setViewport(0, 0, mWidth, mHeight);

        // Attach SSR texture as render target
        mSSRCamera->attach(osg::Camera::COLOR_BUFFER0, mSSRTexture.get());

        // Set up orthographic projection for fullscreen quad
        mSSRCamera->setProjectionMatrix(osg::Matrix::ortho2D(0, 1, 0, 1));
        mSSRCamera->setViewMatrix(osg::Matrix::identity());

        mSSRCamera->setNodeMask(Mask_RenderToTexture);

        // Create fullscreen quad
        mFullscreenQuad = new osg::Geometry;
        mFullscreenQuad->setUseDisplayList(false);
        mFullscreenQuad->setUseVertexBufferObjects(true);

        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array(4);
        (*vertices)[0] = osg::Vec3(0, 0, 0);
        (*vertices)[1] = osg::Vec3(1, 0, 0);
        (*vertices)[2] = osg::Vec3(1, 1, 0);
        (*vertices)[3] = osg::Vec3(0, 1, 0);
        mFullscreenQuad->setVertexArray(vertices);

        osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array(4);
        (*texcoords)[0] = osg::Vec2(0, 0);
        (*texcoords)[1] = osg::Vec2(1, 0);
        (*texcoords)[2] = osg::Vec2(1, 1);
        (*texcoords)[3] = osg::Vec2(0, 1);
        mFullscreenQuad->setTexCoordArray(0, texcoords);

        mFullscreenQuad->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, 4));

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(mFullscreenQuad);
        mSSRCamera->addChild(geode);
    }

    void SSRManager::createSSRShader()
    {
        osg::StateSet* stateset = mFullscreenQuad->getOrCreateStateSet();

        // Create shader program
        Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();

        // For now, use a simple passthrough - actual SSR shader will be added later
        osg::ref_ptr<osg::Program> program = new osg::Program;

        osg::ref_ptr<osg::Shader> vertShader = new osg::Shader(osg::Shader::VERTEX);
        vertShader->setShaderSource(R"(
            #version 120
            void main() {
                gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
                gl_TexCoord[0] = gl_MultiTexCoord0;
            }
        )");

        // Simple placeholder fragment shader - will be replaced with actual SSR implementation
        osg::ref_ptr<osg::Shader> fragShader = new osg::Shader(osg::Shader::FRAGMENT);
        fragShader->setShaderSource(R"(
            #version 120
            uniform sampler2D colorBuffer;
            void main() {
                vec2 uv = gl_TexCoord[0].xy;
                vec3 color = texture2D(colorBuffer, uv).rgb;
                gl_FragColor = vec4(color, 1.0); // Full confidence for now
            }
        )");

        program->addShader(vertShader);
        program->addShader(fragShader);
        stateset->setAttributeAndModes(program, osg::StateAttribute::ON);

        // Create uniforms
        mViewMatrixUniform = new osg::Uniform("viewMatrix", osg::Matrixf());
        mProjectionMatrixUniform = new osg::Uniform("projectionMatrix", osg::Matrixf());
        mInvViewProjectionUniform = new osg::Uniform("invViewProjection", osg::Matrixf());
        mMaxDistanceUniform = new osg::Uniform("maxDistance", mParams.maxDistance);
        mMaxStepsUniform = new osg::Uniform("maxSteps", mParams.maxSteps);
        mStepSizeUniform = new osg::Uniform("stepSize", mParams.stepSize);
        mThicknessUniform = new osg::Uniform("thickness", mParams.thickness);
        mFadeParamsUniform = new osg::Uniform("fadeParams", osg::Vec2f(mParams.fadeStart, mParams.fadeEnd));

        stateset->addUniform(mViewMatrixUniform);
        stateset->addUniform(mProjectionMatrixUniform);
        stateset->addUniform(mInvViewProjectionUniform);
        stateset->addUniform(mMaxDistanceUniform);
        stateset->addUniform(mMaxStepsUniform);
        stateset->addUniform(mStepSizeUniform);
        stateset->addUniform(mThicknessUniform);
        stateset->addUniform(mFadeParamsUniform);

        // Texture unit assignments
        stateset->addUniform(new osg::Uniform("colorBuffer", 0));
        stateset->addUniform(new osg::Uniform("depthBuffer", 1));
        stateset->addUniform(new osg::Uniform("normalBuffer", 2));
    }

    void SSRManager::setInputTextures(
        osg::Texture2D* colorBuffer, osg::Texture2D* depthBuffer, osg::Texture2D* normalBuffer)
    {
        mColorBuffer = colorBuffer;
        mDepthBuffer = depthBuffer;
        mNormalBuffer = normalBuffer;

        osg::StateSet* stateset = mFullscreenQuad->getOrCreateStateSet();

        if (mColorBuffer)
            stateset->setTextureAttributeAndModes(0, mColorBuffer, osg::StateAttribute::ON);

        if (mDepthBuffer)
            stateset->setTextureAttributeAndModes(1, mDepthBuffer, osg::StateAttribute::ON);

        if (mNormalBuffer)
            stateset->setTextureAttributeAndModes(2, mNormalBuffer, osg::StateAttribute::ON);
    }

    void SSRManager::setParams(const Params& params)
    {
        mParams = params;
        updateUniforms();
    }

    void SSRManager::updateUniforms()
    {
        if (mMaxDistanceUniform)
            mMaxDistanceUniform->set(mParams.maxDistance);
        if (mMaxStepsUniform)
            mMaxStepsUniform->set(mParams.maxSteps);
        if (mStepSizeUniform)
            mStepSizeUniform->set(mParams.stepSize);
        if (mThicknessUniform)
            mThicknessUniform->set(mParams.thickness);
        if (mFadeParamsUniform)
            mFadeParamsUniform->set(osg::Vec2f(mParams.fadeStart, mParams.fadeEnd));
    }

    void SSRManager::update(const osg::Matrix& viewMatrix, const osg::Matrix& projectionMatrix)
    {
        if (!mParams.enabled)
            return;

        // Update matrix uniforms
        mViewMatrixUniform->set(osg::Matrixf(viewMatrix));
        mProjectionMatrixUniform->set(osg::Matrixf(projectionMatrix));

        osg::Matrix invViewProj = osg::Matrix::inverse(viewMatrix * projectionMatrix);
        mInvViewProjectionUniform->set(osg::Matrixf(invViewProj));
    }

    void SSRManager::setEnabled(bool enabled)
    {
        mParams.enabled = enabled;
        if (mSSRCamera)
            mSSRCamera->setNodeMask(enabled ? Mask_RenderToTexture : 0);
    }

    void SSRManager::resize(int width, int height)
    {
        if (width == mWidth && height == mHeight)
            return;

        mWidth = width;
        mHeight = height;

        createRenderTargets(width, height);

        if (mSSRCamera)
        {
            mSSRCamera->setViewport(0, 0, width, height);
            mSSRCamera->detach(osg::Camera::COLOR_BUFFER0);
            mSSRCamera->attach(osg::Camera::COLOR_BUFFER0, mSSRTexture.get());
        }
    }
}
