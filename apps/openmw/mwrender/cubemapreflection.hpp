#ifndef OPENMW_MWRENDER_CUBEMAPREFLECTION_HPP
#define OPENMW_MWRENDER_CUBEMAPREFLECTION_HPP

#include <osg/Camera>
#include <osg/Group>
#include <osg/NodeCallback>
#include <osg/TextureCubeMap>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <osgUtil/CullVisitor>

#include <vector>

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    /**
     * @brief Cull callback to traverse scene without circular reference
     *
     * This callback allows the cubemap camera to render the scene without
     * adding the scene as a child node, avoiding circular scene graph references.
     */
    class CubemapCullCallback : public osg::NodeCallback
    {
    public:
        CubemapCullCallback(osg::Group* sceneRoot)
            : mSceneRoot(sceneRoot)
        {
        }

        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) override
        {
            osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>(nv);
            if (cv && mSceneRoot.valid())
            {
                // Traverse scene without adding as child - breaks circular reference
                mSceneRoot->accept(*cv);
            }
            traverse(node, nv);
        }

    private:
        osg::observer_ptr<osg::Group> mSceneRoot;
    };

    /**
     * @brief Manages environment cubemaps for water reflections
     *
     * Creates and maintains cubemaps at strategic positions to provide
     * fallback reflections when SSR misses or is unavailable.
     *
     * Features:
     * - Dynamic cubemap updates
     * - Region-based cubemap placement
     * - Lazy update strategy for performance
     * - Automatic nearest cubemap selection
     */
    class CubemapReflectionManager
    {
    public:
        /**
         * @brief Cubemap region descriptor
         */
        struct CubemapRegion
        {
            osg::Vec3f center;                          ///< World position of cubemap
            float radius;                               ///< Influence radius
            osg::ref_ptr<osg::TextureCubeMap> cubemap;  ///< The cubemap texture
            osg::ref_ptr<osg::Camera> renderCameras[6]; ///< Cameras for each face
            float updateInterval;                       ///< Seconds between updates
            float timeSinceUpdate;                      ///< Time since last update
            bool needsUpdate;                           ///< Dirty flag

            CubemapRegion()
                : center(0, 0, 0)
                , radius(1000.0f)
                , updateInterval(5.0f)
                , timeSinceUpdate(0.0f)
                , needsUpdate(true)
            {
            }
        };

        /**
         * @brief Cubemap quality settings
         */
        struct Params
        {
            int resolution = 512;         ///< Cubemap face resolution (per face)
            float updateInterval = 5.0f;  ///< Default update interval in seconds
            int maxRegions = 8;           ///< Maximum number of cubemap regions
            bool enabled = true;          ///< Master enable/disable
            bool dynamicUpdates = true;   ///< Enable dynamic updates
        };

        CubemapReflectionManager(osg::Group* parent, osg::Group* sceneRoot,
            Resource::ResourceSystem* resourceSystem);
        ~CubemapReflectionManager();

        /**
         * @brief Initialize cubemap system
         */
        void initialize();

        /**
         * @brief Set cubemap parameters
         */
        void setParams(const Params& params);
        const Params& getParams() const { return mParams; }

        /**
         * @brief Add or update a cubemap region
         * @param center World position
         * @param radius Influence radius
         * @return Region index
         */
        int addRegion(const osg::Vec3f& center, float radius);

        /**
         * @brief Remove a cubemap region
         */
        void removeRegion(int index);

        /**
         * @brief Get cubemap for a given position
         * @param pos World position
         * @return Nearest cubemap, or nullptr if none available
         */
        osg::TextureCubeMap* getCubemapForPosition(const osg::Vec3f& pos) const;

        /**
         * @brief Force update of a specific region
         */
        void updateRegion(int index);

        /**
         * @brief Update cubemap system
         * @param dt Delta time in seconds
         * @param cameraPos Current camera position for prioritization
         */
        void update(float dt, const osg::Vec3f& cameraPos);

        /**
         * @brief Enable or disable cubemap rendering
         */
        void setEnabled(bool enabled);
        bool isEnabled() const { return mParams.enabled; }

        /**
         * @brief Clear all regions
         */
        void clearRegions();

        /**
         * @brief Get number of active regions
         */
        size_t getRegionCount() const { return mRegions.size(); }

    private:
        void createCubemapRegion(CubemapRegion& region);
        void renderCubemap(CubemapRegion& region);
        int findNearestRegionIndex(const osg::Vec3f& pos) const;

        osg::ref_ptr<osg::Group> mParent;
        osg::ref_ptr<osg::Group> mSceneRoot;
        Resource::ResourceSystem* mResourceSystem;

        std::vector<CubemapRegion> mRegions;
        Params mParams;

        // Default fallback cubemap (sky only)
        osg::ref_ptr<osg::TextureCubeMap> mFallbackCubemap;
    };
}

#endif
