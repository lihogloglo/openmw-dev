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
        mSSRCamera->setRenderOrder(osg::Camera::PRE_RENDER, -50); // Before water, after scene
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

        // Try to use ShaderManager to load the SSR shader
        Shader::ShaderManager& shaderMgr = mResourceSystem->getSceneManager()->getShaderManager();

        osg::ref_ptr<osg::Program> program = new osg::Program;
        program->setName("SSR");

        // Try to load from shader files
        std::map<std::string, std::string> defineMap;

        osg::ref_ptr<osg::Shader> vertShader = shaderMgr.getShader("ssr_fullscreen.vert", defineMap, osg::Shader::VERTEX);
        osg::ref_ptr<osg::Shader> fragShader = shaderMgr.getShader("ssr_raymarch.frag", defineMap, osg::Shader::FRAGMENT);

        // If shader files not found, use inline fallback with actual SSR implementation
        if (!vertShader)
        {
            vertShader = new osg::Shader(osg::Shader::VERTEX);
            vertShader->setShaderSource(R"(
                #version 120
                varying vec2 vTexCoord;
                void main() {
                    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
                    vTexCoord = gl_MultiTexCoord0.xy;
                }
            )");
        }

        if (!fragShader)
        {
            // Use the actual SSR raymarch implementation inline
            fragShader = new osg::Shader(osg::Shader::FRAGMENT);
            fragShader->setShaderSource(R"(
                #version 120

                uniform sampler2D colorBuffer;
                uniform sampler2D depthBuffer;

                uniform mat4 viewMatrix;
                uniform mat4 projectionMatrix;
                uniform mat4 invViewProjection;

                uniform float maxDistance;
                uniform int maxSteps;
                uniform float stepSize;
                uniform float thickness;
                uniform vec2 fadeParams;

                varying vec2 vTexCoord;

                const float NEAR_PLANE = 1.0;
                const float FAR_PLANE = 7168.0;

                float linearizeDepth(float depth)
                {
                    float z = depth * 2.0 - 1.0;
                    return (2.0 * NEAR_PLANE * FAR_PLANE) / (FAR_PLANE + NEAR_PLANE - z * (FAR_PLANE - NEAR_PLANE));
                }

                vec3 getWorldPosition(vec2 uv, float depth)
                {
                    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                    vec4 worldPos = invViewProjection * clipPos;
                    return worldPos.xyz / worldPos.w;
                }

                vec3 projectToScreen(vec3 worldPos)
                {
                    vec4 clipPos = projectionMatrix * (viewMatrix * vec4(worldPos, 1.0));
                    vec3 ndcPos = clipPos.xyz / clipPos.w;
                    return vec3(ndcPos.xy * 0.5 + 0.5, ndcPos.z * 0.5 + 0.5);
                }

                float screenEdgeFade(vec2 uv)
                {
                    vec2 distToEdge = min(uv, 1.0 - uv);
                    float minDist = min(distToEdge.x, distToEdge.y);
                    return smoothstep(0.0, 0.1, minDist);
                }

                void main()
                {
                    vec2 uv = vTexCoord;
                    float depth = texture2D(depthBuffer, uv).r;

                    if (depth >= 0.9999)
                    {
                        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
                        return;
                    }

                    vec3 worldPos = getWorldPosition(uv, depth);

                    // Assume water normal points up (0, 0, 1)
                    vec3 normal = vec3(0.0, 0.0, 1.0);

                    // Get camera position from view matrix (translation is negated in column 3)
                    // For view matrix, camera position is -transpose(rotation) * translation
                    vec3 cameraPos = -vec3(
                        dot(viewMatrix[0].xyz, viewMatrix[3].xyz),
                        dot(viewMatrix[1].xyz, viewMatrix[3].xyz),
                        dot(viewMatrix[2].xyz, viewMatrix[3].xyz)
                    );

                    vec3 viewDir = normalize(worldPos - cameraPos);
                    vec3 reflectDir = reflect(viewDir, normal);

                    // Raymarch
                    float stepLen = maxDistance / float(maxSteps);
                    vec3 currentPos = worldPos;

                    for (int i = 0; i < 64; ++i)
                    {
                        currentPos += reflectDir * stepLen;

                        vec3 screenPos = projectToScreen(currentPos);

                        if (screenPos.x < 0.0 || screenPos.x > 1.0 ||
                            screenPos.y < 0.0 || screenPos.y > 1.0 ||
                            screenPos.z < 0.0 || screenPos.z > 1.0)
                        {
                            break;
                        }

                        float sampledDepth = texture2D(depthBuffer, screenPos.xy).r;
                        float sampledLinear = linearizeDepth(sampledDepth);
                        float rayLinear = linearizeDepth(screenPos.z);

                        float depthDiff = rayLinear - sampledLinear;
                        if (depthDiff > 0.0 && depthDiff < thickness * 10.0)
                        {
                            vec3 color = texture2D(colorBuffer, screenPos.xy).rgb;
                            float edgeFade = screenEdgeFade(screenPos.xy);
                            float distFade = 1.0 - float(i) / 64.0;
                            float confidence = edgeFade * distFade * 0.8;

                            gl_FragColor = vec4(color, confidence);
                            return;
                        }
                    }

                    gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
                }
            )");
        }

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
