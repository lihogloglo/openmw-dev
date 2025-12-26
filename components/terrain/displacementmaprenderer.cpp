#include "displacementmaprenderer.hpp"

#include <osg/FrameBufferObject>
#include <osg/RenderInfo>
#include <osg/Texture2D>

#include <algorithm>

namespace Terrain
{

    DisplacementMapRenderer::DisplacementMapRenderer()
        : mTargetFrameRate(120)
        , mMinimumTimeAvailable(0.0025)
    {
        setSupportsDisplayList(false);
        setCullingActive(false);

        mFBO = new osg::FrameBufferObject;

        getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    }

    DisplacementMapRenderer::~DisplacementMapRenderer() {}

    void DisplacementMapRenderer::drawImplementation(osg::RenderInfo& renderInfo) const
    {
        double dt = mTimer.time_s();
        dt = std::min(dt, 0.2);
        mTimer.setStartTick();
        double targetFrameTime = 1.0 / static_cast<double>(mTargetFrameRate);
        double conservativeTimeRatio(0.75);
        double availableTime = std::max((targetFrameTime - dt) * conservativeTimeRatio, mMinimumTimeAvailable);

        std::lock_guard<std::mutex> lock(mMutex);

        if (mImmediateCompileSet.empty() && mCompileSet.empty())
            return;

        while (!mImmediateCompileSet.empty())
        {
            osg::ref_ptr<DisplacementMap> node = *mImmediateCompileSet.begin();
            mImmediateCompileSet.erase(node);

            mMutex.unlock();
            compile(*node, renderInfo);
            mMutex.lock();
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(availableTime);
        while (!mCompileSet.empty() && std::chrono::steady_clock::now() < deadline)
        {
            osg::ref_ptr<DisplacementMap> node = *mCompileSet.begin();
            mCompileSet.erase(node);

            mMutex.unlock();
            compile(*node, renderInfo);
            mMutex.lock();

            if (node->mCompiled < node->mDrawables.size())
            {
                // We did not compile the map fully.
                // Place it back to queue to continue work in the next time.
                mCompileSet.insert(node);
            }
        }
        mTimer.setStartTick();
    }

    void DisplacementMapRenderer::compile(DisplacementMap& displacementMap, osg::RenderInfo& renderInfo) const
    {
        // if there are no more external references we can assume the texture is no longer required
        if (displacementMap.mTexture->referenceCount() <= 1)
        {
            displacementMap.mCompiled = static_cast<unsigned int>(displacementMap.mDrawables.size());
            return;
        }

        osg::Timer timer;
        osg::State& state = *renderInfo.getState();
        osg::GLExtensions* ext = state.get<osg::GLExtensions>();

        if (!mFBO)
            return;

        if (!ext->isFrameBufferObjectSupported)
            return;

        osg::FrameBufferAttachment attach(displacementMap.mTexture);
        mFBO->setAttachment(osg::Camera::COLOR_BUFFER, attach);
        mFBO->apply(state, osg::FrameBufferObject::DRAW_FRAMEBUFFER);

        GLenum status = ext->glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);

        if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
        {
            GLuint fboId = state.getGraphicsContext() ? state.getGraphicsContext()->getDefaultFboId() : 0;
            ext->glBindFramebuffer(GL_FRAMEBUFFER_EXT, fboId);
            OSG_ALWAYS << "Error attaching FBO for displacement map" << std::endl;
            return;
        }

        // inform State that Texture attribute has changed due to compiling of FBO texture
        state.haveAppliedTextureAttribute(state.getActiveTextureUnit(), osg::StateAttribute::TEXTURE);

        // Clear to neutral height (0.5 = no displacement)
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        for (unsigned int i = displacementMap.mCompiled; i < displacementMap.mDrawables.size(); ++i)
        {
            osg::Drawable* drw = displacementMap.mDrawables[i];
            osg::StateSet* stateset = drw->getStateSet();

            if (stateset)
                renderInfo.getState()->pushStateSet(stateset);

            renderInfo.getState()->apply();

            glViewport(0, 0, displacementMap.mTexture->getTextureWidth(), displacementMap.mTexture->getTextureHeight());
            drw->drawImplementation(renderInfo);

            if (stateset)
                renderInfo.getState()->popStateSet();

            ++displacementMap.mCompiled;

            displacementMap.mDrawables[i] = nullptr;
        }
        if (displacementMap.mCompiled == displacementMap.mDrawables.size())
            displacementMap.mDrawables = std::vector<osg::ref_ptr<osg::Drawable>>();

        state.haveAppliedAttribute(osg::StateAttribute::VIEWPORT);

        GLuint fboId = state.getGraphicsContext() ? state.getGraphicsContext()->getDefaultFboId() : 0;
        ext->glBindFramebuffer(GL_FRAMEBUFFER_EXT, fboId);
    }

    void DisplacementMapRenderer::setMinimumTimeAvailableForCompile(double time)
    {
        mMinimumTimeAvailable = time;
    }

    void DisplacementMapRenderer::setTargetFrameRate(float framerate)
    {
        mTargetFrameRate = framerate;
    }

    void DisplacementMapRenderer::addDisplacementMap(DisplacementMap* displacementMap, bool immediate)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (immediate)
            mImmediateCompileSet.insert(displacementMap);
        else
            mCompileSet.insert(displacementMap);
    }

    void DisplacementMapRenderer::setImmediate(DisplacementMap* displacementMap)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        CompileSet::iterator found = mCompileSet.find(displacementMap);
        if (found == mCompileSet.end())
            return;
        else
        {
            mImmediateCompileSet.insert(displacementMap);
            mCompileSet.erase(found);
        }
    }

    unsigned int DisplacementMapRenderer::getCompileSetSize() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<unsigned int>(mCompileSet.size());
    }

    DisplacementMap::DisplacementMap()
        : mCompiled(0)
    {
    }

    DisplacementMap::~DisplacementMap() {}

}
