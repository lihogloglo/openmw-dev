#include "cubemapreflection.hpp"

#include <algorithm>
#include <limits>

#include <osg/CullFace>
#include <osg/Matrix>
#include <osg/Texture>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/debug/debuglog.hpp>
#include <components/sceneutil/depth.hpp>

#include "vismask.hpp"

namespace MWRender
{
    namespace
    {
        // Cubemap face directions and up vectors
        const osg::Vec3f FACE_DIRS[6] = { osg::Vec3f(1, 0, 0),  // +X
            osg::Vec3f(-1, 0, 0),                               // -X
            osg::Vec3f(0, 1, 0),                                // +Y
            osg::Vec3f(0, -1, 0),                               // -Y
            osg::Vec3f(0, 0, 1),                                // +Z
            osg::Vec3f(0, 0, -1) };                             // -Z

        const osg::Vec3f FACE_UPS[6] = { osg::Vec3f(0, 0, -1), // +X
            osg::Vec3f(0, 0, -1),                              // -X
            osg::Vec3f(0, 0, 1),                               // +Y
            osg::Vec3f(0, 0, -1),                              // -Y
            osg::Vec3f(0, 1, 0),                               // +Z
            osg::Vec3f(0, -1, 0) };                            // -Z
    }

    CubemapReflectionManager::CubemapReflectionManager(
        osg::Group* parent, osg::Group* sceneRoot, Resource::ResourceSystem* resourceSystem)
        : mParent(parent)
        , mSceneRoot(sceneRoot)
        , mResourceSystem(resourceSystem)
    {
    }

    CubemapReflectionManager::~CubemapReflectionManager()
    {
        clearRegions();
    }

    void CubemapReflectionManager::initialize()
    {
        Log(Debug::Info) << "[Cubemap] Initializing CubemapReflectionManager with resolution " << mParams.resolution;

        // Create a simple fallback cubemap initialized with neutral gray color
        mFallbackCubemap = new osg::TextureCubeMap;
        mFallbackCubemap->setTextureSize(mParams.resolution, mParams.resolution);
        mFallbackCubemap->setInternalFormat(GL_RGB8);
        mFallbackCubemap->setSourceFormat(GL_RGB);
        mFallbackCubemap->setSourceType(GL_UNSIGNED_BYTE);
        mFallbackCubemap->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
        mFallbackCubemap->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        mFallbackCubemap->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        mFallbackCubemap->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        mFallbackCubemap->setWrap(osg::Texture::WRAP_R, osg::Texture::CLAMP_TO_EDGE);

        // Initialize all 6 faces with a light neutral gray color (RGB: 180, 180, 180)
        // This prevents the cubemap from being black when no regions are active
        // We use light gray instead of sky blue to avoid overly blue water appearance
        int resolution = mParams.resolution;
        std::vector<unsigned char> neutralGrayData(resolution * resolution * 3);
        for (int i = 0; i < resolution * resolution; ++i)
        {
            neutralGrayData[i * 3 + 0] = 180; // R
            neutralGrayData[i * 3 + 1] = 180; // G
            neutralGrayData[i * 3 + 2] = 180; // B
        }

        for (int face = 0; face < 6; ++face)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setImage(resolution, resolution, 1, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE,
                neutralGrayData.data(), osg::Image::NO_DELETE);
            mFallbackCubemap->setImage(face, image);
        }

        Log(Debug::Info) << "[Cubemap] Fallback cubemap initialized with neutral gray (180,180,180)";
    }

    void CubemapReflectionManager::setParams(const Params& params)
    {
        mParams = params;
    }

    void CubemapReflectionManager::createCubemapRegion(CubemapRegion& region)
    {
        // Create cubemap texture
        region.cubemap = new osg::TextureCubeMap;
        region.cubemap->setTextureSize(mParams.resolution, mParams.resolution);
        region.cubemap->setInternalFormat(GL_RGB8);
        region.cubemap->setSourceFormat(GL_RGB);
        region.cubemap->setSourceType(GL_UNSIGNED_BYTE);
        region.cubemap->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
        region.cubemap->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        region.cubemap->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        region.cubemap->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        region.cubemap->setWrap(osg::Texture::WRAP_R, osg::Texture::CLAMP_TO_EDGE);

        // Create cameras for each cubemap face
        for (int face = 0; face < 6; ++face)
        {
            osg::Camera* camera = new osg::Camera;
            camera->setName("Cubemap Face " + std::to_string(face));
            camera->setRenderOrder(osg::Camera::PRE_RENDER, -200); // Render before main scene and SSR
            camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT, osg::Camera::PIXEL_BUFFER_RTT);
            camera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
            SceneUtil::setCameraClearDepth(camera);
            camera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            camera->setClearColor(osg::Vec4(0.7f, 0.7f, 0.7f, 1.0f)); // Neutral gray to avoid overly blue reflections
            camera->setViewport(0, 0, mParams.resolution, mParams.resolution);
            camera->setComputeNearFarMode(osg::Camera::DO_NOT_COMPUTE_NEAR_FAR);

            // Set culling mode to match localmap (enable far plane culling, disable small feature culling)
            osg::Camera::CullingMode cullingMode
                = (osg::Camera::DEFAULT_CULLING | osg::Camera::FAR_PLANE_CULLING) & ~(osg::Camera::SMALL_FEATURE_CULLING);
            camera->setCullingMode(cullingMode);

            // Attach to cubemap face
            camera->attach(osg::Camera::COLOR_BUFFER,
                region.cubemap.get(), 0, static_cast<osg::TextureCubeMap::Face>(face));

            // Set up 90-degree FOV for cubemap face
            camera->setProjectionMatrixAsPerspective(90.0, 1.0, 0.1, 10000.0);

            // Set view matrix for this face
            osg::Vec3f eye = region.center;
            osg::Vec3f center = eye + FACE_DIRS[face];
            osg::Vec3f up = FACE_UPS[face];
            camera->setViewMatrixAsLookAt(eye, center, up);

            // Use cull callback instead of addChild to avoid circular reference
            camera->setCullCallback(new CubemapCullCallback(mSceneRoot));

            // Set cull mask (exclude water, UI, etc. to avoid recursion)
            camera->setCullMask(
                Mask_Scene | Mask_Object | Mask_Static | Mask_Terrain | Mask_Actor | Mask_Sky | Mask_Lighting);

            // Start with camera DISABLED - will be enabled when needed
            camera->setNodeMask(0);

            region.renderCameras[face] = camera;

            // Add camera to parent (not scene root, avoiding circular reference)
            if (mParent)
                mParent->addChild(camera);
        }

        region.needsUpdate = true;
        region.updateInterval = mParams.updateInterval;
    }

    int CubemapReflectionManager::addRegion(const osg::Vec3f& center, float radius)
    {
        if (mRegions.size() >= static_cast<size_t>(mParams.maxRegions))
        {
            Log(Debug::Warning) << "[Cubemap] Cannot add region - max regions reached (" << mParams.maxRegions << ")";
            return -1;
        }

        CubemapRegion region;
        region.center = center;
        region.radius = radius;

        createCubemapRegion(region);

        mRegions.push_back(region);
        int index = static_cast<int>(mRegions.size() - 1);

        Log(Debug::Info) << "[Cubemap] Added region #" << index << " at (" << center.x() << ", " << center.y() << ", " << center.z() << ") radius=" << radius;

        return index;
    }

    void CubemapReflectionManager::removeRegion(int index)
    {
        if (index < 0 || index >= static_cast<int>(mRegions.size()))
            return;

        CubemapRegion& region = mRegions[index];

        // Remove cameras from parent
        if (mParent)
        {
            for (int face = 0; face < 6; ++face)
            {
                if (region.renderCameras[face])
                    mParent->removeChild(region.renderCameras[face]);
            }
        }

        mRegions.erase(mRegions.begin() + index);
    }

    void CubemapReflectionManager::clearRegions()
    {
        while (!mRegions.empty())
            removeRegion(0);
    }

    int CubemapReflectionManager::findNearestRegionIndex(const osg::Vec3f& pos) const
    {
        if (mRegions.empty())
            return -1;

        int nearest = 0;
        float nearestDist = (mRegions[0].center - pos).length();

        for (size_t i = 1; i < mRegions.size(); ++i)
        {
            float dist = (mRegions[i].center - pos).length();
            if (dist < nearestDist)
            {
                nearest = static_cast<int>(i);
                nearestDist = dist;
            }
        }

        // Check if within radius
        if (nearestDist <= mRegions[nearest].radius)
            return nearest;

        return -1; // No region in range
    }

    osg::TextureCubeMap* CubemapReflectionManager::getCubemapForPosition(const osg::Vec3f& pos) const
    {
        int index = findNearestRegionIndex(pos);
        if (index >= 0)
            return mRegions[index].cubemap.get();

        return mFallbackCubemap.get();
    }

    void CubemapReflectionManager::renderCubemap(CubemapRegion& region)
    {
        if (!mParams.dynamicUpdates || !mParams.enabled)
        {
            Log(Debug::Verbose) << "[Cubemap] renderCubemap skipped - dynamicUpdates=" << mParams.dynamicUpdates << " enabled=" << mParams.enabled;
            return;
        }

        Log(Debug::Info) << "[Cubemap] Rendering cubemap at (" << region.center.x() << ", " << region.center.y() << ", " << region.center.z() << ")";

        // Enable cameras persistently - they will stay enabled and render every frame
        // This is OK performance-wise because:
        // 1. We only enable ONE region per update cycle (see update() function)
        // 2. The updateInterval (default 5 seconds) controls how often we switch to different regions
        // 3. Having the nearest region's cubemap update every frame ensures smooth reflections
        for (int face = 0; face < 6; ++face)
        {
            if (region.renderCameras[face])
            {
                // Update camera position (in case region moved)
                osg::Vec3f eye = region.center;
                osg::Vec3f center = eye + FACE_DIRS[face];
                osg::Vec3f up = FACE_UPS[face];
                region.renderCameras[face]->setViewMatrixAsLookAt(eye, center, up);

                // Enable camera persistently
                region.renderCameras[face]->setNodeMask(Mask_RenderToTexture);
                Log(Debug::Verbose) << "[Cubemap]   Enabled camera face " << face << " (will render every frame)";
            }
        }

        region.needsUpdate = false;
        region.timeSinceUpdate = 0.0f;
        region.camerasActive = true;

        Log(Debug::Info) << "[Cubemap] Cubemap cameras enabled - will render continuously until replaced";
    }

    void CubemapReflectionManager::updateRegion(int index)
    {
        if (index < 0 || index >= static_cast<int>(mRegions.size()))
            return;

        mRegions[index].needsUpdate = true;
    }

    void CubemapReflectionManager::update(float dt, const osg::Vec3f& cameraPos)
    {
        if (!mParams.enabled)
            return;

        static int frameCount = 0;
        static bool loggedOnce = false;
        static int lastActiveRegion = -1;
        frameCount++;
        bool shouldLog = (frameCount % 300 == 0); // Log every 5 seconds at 60fps

        if (!loggedOnce && mRegions.size() > 0)
        {
            Log(Debug::Info) << "[Cubemap] First update() call - " << mRegions.size() << " regions active";
            loggedOnce = true;
        }

        // Find the nearest region to determine which one should be active
        int nearestIndex = findNearestRegionIndex(cameraPos);

        // If we switched to a different region, disable the old one's cameras
        if (nearestIndex != lastActiveRegion && lastActiveRegion >= 0 && lastActiveRegion < static_cast<int>(mRegions.size()))
        {
            if (shouldLog)
                Log(Debug::Info) << "[Cubemap] Switching from region #" << lastActiveRegion << " to region #" << nearestIndex;

            // Disable cameras from the previously active region
            CubemapRegion& oldRegion = mRegions[lastActiveRegion];
            for (int face = 0; face < 6; ++face)
            {
                if (oldRegion.renderCameras[face])
                    oldRegion.renderCameras[face]->setNodeMask(0);
            }
            oldRegion.camerasActive = false;
        }

        lastActiveRegion = nearestIndex;

        // Update timers for inactive regions only
        // Active region doesn't need timer-based updates since it renders every frame
        int needsUpdateCount = 0;
        for (size_t i = 0; i < mRegions.size(); ++i)
        {
            // Skip the currently active region - it's already rendering
            if (static_cast<int>(i) == nearestIndex && mRegions[i].camerasActive)
                continue;

            mRegions[i].timeSinceUpdate += dt;

            if (mRegions[i].timeSinceUpdate >= mRegions[i].updateInterval)
            {
                mRegions[i].needsUpdate = true;
                needsUpdateCount++;
            }
        }

        if (needsUpdateCount > 0 && shouldLog)
        {
            Log(Debug::Info) << "[Cubemap] " << needsUpdateCount << " inactive regions need update (interval=" << mParams.updateInterval << "s)";
        }

        // Enable nearest region if it's not active yet
        if (nearestIndex >= 0 && !mRegions[nearestIndex].camerasActive)
        {
            if (shouldLog)
                Log(Debug::Info) << "[Cubemap] Activating nearest region #" << nearestIndex;
            renderCubemap(mRegions[nearestIndex]);
            return;
        }

        // Nearest region is already active and rendering continuously - nothing to do
    }

    void CubemapReflectionManager::setEnabled(bool enabled)
    {
        mParams.enabled = enabled;

        // Disable all cameras when disabled
        if (!enabled)
        {
            for (auto& region : mRegions)
            {
                for (int face = 0; face < 6; ++face)
                {
                    if (region.renderCameras[face])
                        region.renderCameras[face]->setNodeMask(0);
                }
            }
        }
    }
}
