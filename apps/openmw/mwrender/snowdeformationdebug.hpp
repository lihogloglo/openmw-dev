#ifndef OPENMW_MWRENDER_SNOWDEFORMATIONDEBUG_HPP
#define OPENMW_MWRENDER_SNOWDEFORMATIONDEBUG_HPP

#include <osg/Group>
#include <osg/Texture2D>
#include <osg/Vec3f>
#include <osg/Geode>

namespace MWRender
{
    class SnowDeformationManager;

    /// Debug visualization for snow deformation system
    class SnowDeformationDebugger
    {
    public:
        SnowDeformationDebugger(osg::ref_ptr<osg::Group> rootNode, SnowDeformationManager* manager);
        ~SnowDeformationDebugger();

        void update(const osg::Vec3f& cameraPos);

        /// Toggle debug visualization on/off
        void setEnabled(bool enabled);
        bool isEnabled() const { return mEnabled; }

        /// Show deformation texture as HUD overlay
        void setShowTextureOverlay(bool show);

        /// Show footprint markers in world
        void setShowFootprintMarkers(bool show);

        /// Show deformation mesh wireframe
        void setShowMeshWireframe(bool show);

        /// Show deformation area bounds
        void setShowDeformationBounds(bool show);

        /// Enable debug shader output (shows deformation as color)
        void setDebugShaderOutput(bool enable);

    private:
        void createTextureOverlay();
        void createFootprintMarkers();
        void createWireframeVisualization();
        void createBoundsVisualization();
        void updateFootprintMarkers(const osg::Vec3f& cameraPos);
        void updateBoundsVisualization();

        osg::ref_ptr<osg::Group> mRootNode;
        osg::ref_ptr<osg::Group> mDebugGroup;
        SnowDeformationManager* mManager;

        // Debug visualization nodes
        osg::ref_ptr<osg::Geode> mTextureOverlayGeode;
        osg::ref_ptr<osg::Group> mFootprintMarkersGroup;
        osg::ref_ptr<osg::Geode> mWireframeGeode;
        osg::ref_ptr<osg::Geode> mBoundsGeode;

        bool mEnabled;
        bool mShowTextureOverlay;
        bool mShowFootprintMarkers;
        bool mShowMeshWireframe;
        bool mShowDeformationBounds;
    };

} // namespace MWRender

#endif // OPENMW_MWRENDER_SNOWDEFORMATIONDEBUG_HPP
