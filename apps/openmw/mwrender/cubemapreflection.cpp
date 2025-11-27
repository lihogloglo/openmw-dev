#include "cubemapreflection.hpp"

#include <algorithm>
#include <limits>

#include <osg/CullFace>
#include <osg/Matrix>
#include <osg/Texture>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>

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
        // Create a simple fallback cubemap initialized with sky blue color
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

        // Initialize all 6 faces with a sky blue color (RGB: 135, 206, 235)
        // This prevents the cubemap from being black when no regions are active
        int resolution = mParams.resolution;
        std::vector<unsigned char> skyBlueData(resolution * resolution * 3);
        for (int i = 0; i < resolution * resolution; ++i)
        {
            skyBlueData[i * 3 + 0] = 135; // R
            skyBlueData[i * 3 + 1] = 206; // G
            skyBlueData[i * 3 + 2] = 235; // B
        }

        for (int face = 0; face < 6; ++face)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setImage(resolution, resolution, 1, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE,
                skyBlueData.data(), osg::Image::NO_DELETE);
            mFallbackCubemap->setImage(face, image);
        }
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
            camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
            camera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
            camera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            camera->setClearColor(osg::Vec4(0.5f, 0.7f, 1.0f, 1.0f)); // Sky blue
            camera->setViewport(0, 0, mParams.resolution, mParams.resolution);

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

            camera->setNodeMask(Mask_RenderToTexture); // Always enabled for RTT

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
            return -1;

        CubemapRegion region;
        region.center = center;
        region.radius = radius;

        createCubemapRegion(region);

        mRegions.push_back(region);
        return static_cast<int>(mRegions.size() - 1);
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
            return;

        // Enable cameras for one frame to render cubemap
        for (int face = 0; face < 6; ++face)
        {
            if (region.renderCameras[face])
            {
                // Update camera position (in case region moved)
                osg::Vec3f eye = region.center;
                osg::Vec3f center = eye + FACE_DIRS[face];
                osg::Vec3f up = FACE_UPS[face];
                region.renderCameras[face]->setViewMatrixAsLookAt(eye, center, up);

                // Enable for this frame
                region.renderCameras[face]->setNodeMask(Mask_RenderToTexture);
            }
        }

        region.needsUpdate = false;
        region.timeSinceUpdate = 0.0f;
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

        // Update timers and mark regions needing updates
        for (auto& region : mRegions)
        {
            region.timeSinceUpdate += dt;

            if (region.timeSinceUpdate >= region.updateInterval)
            {
                region.needsUpdate = true;
            }

            // Disable cameras after rendering (they render for one frame only)
            for (int face = 0; face < 6; ++face)
            {
                if (region.renderCameras[face])
                    region.renderCameras[face]->setNodeMask(0);
            }
        }

        // Update nearest region with highest priority
        int nearestIndex = findNearestRegionIndex(cameraPos);
        if (nearestIndex >= 0 && mRegions[nearestIndex].needsUpdate)
        {
            renderCubemap(mRegions[nearestIndex]);
            return; // Only update one cubemap per frame for performance
        }

        // Update any other regions that need it
        for (auto& region : mRegions)
        {
            if (region.needsUpdate)
            {
                renderCubemap(region);
                return; // One cubemap per frame
            }
        }
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
