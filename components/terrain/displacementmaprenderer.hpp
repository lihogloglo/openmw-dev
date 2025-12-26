#ifndef OPENMW_COMPONENTS_TERRAIN_DISPLACEMENTMAPRENDERER_H
#define OPENMW_COMPONENTS_TERRAIN_DISPLACEMENTMAPRENDERER_H

#include <osg/Drawable>

#include <mutex>
#include <set>

namespace osg
{
    class FrameBufferObject;
    class RenderInfo;
    class Texture2D;
}

namespace Terrain
{

    class DisplacementMap : public osg::Referenced
    {
    public:
        DisplacementMap();
        ~DisplacementMap();
        std::vector<osg::ref_ptr<osg::Drawable>> mDrawables;
        osg::ref_ptr<osg::Texture2D> mTexture;
        unsigned int mCompiled;
    };

    /**
     * @brief The DisplacementMapRenderer is responsible for rendering blended displacement maps
     * for tessellated terrain. It combines the alpha channels of all terrain layer normal maps,
     * weighted by their blend maps, into a single displacement texture.
     */
    class DisplacementMapRenderer : public osg::Drawable
    {
    public:
        DisplacementMapRenderer();
        ~DisplacementMapRenderer();

        void drawImplementation(osg::RenderInfo& renderInfo) const override;

        void compile(DisplacementMap& displacementMap, osg::RenderInfo& renderInfo) const;

        /// Set the available time in seconds for compiling (non-immediate) displacement maps each frame
        void setMinimumTimeAvailableForCompile(double time);

        /// If current frame rate is higher than this, the extra time will be set aside to do more compiling
        void setTargetFrameRate(float framerate);

        /// Add a displacement map to be rendered
        void addDisplacementMap(DisplacementMap* map, bool immediate = false);

        /// Mark this displacement map to be required for the current frame
        void setImmediate(DisplacementMap* map);

        unsigned int getCompileSetSize() const;

    private:
        float mTargetFrameRate;
        double mMinimumTimeAvailable;
        mutable osg::Timer mTimer;

        typedef std::set<osg::ref_ptr<DisplacementMap>> CompileSet;

        mutable CompileSet mCompileSet;
        mutable CompileSet mImmediateCompileSet;

        mutable std::mutex mMutex;

        osg::ref_ptr<osg::FrameBufferObject> mFBO;
    };

}

#endif
