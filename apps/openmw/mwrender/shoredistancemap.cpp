#include "shoredistancemap.hpp"

#include <osg/Image>
#include <osg/Vec2i>

#include <components/esmterrain/storage.hpp>
#include <components/esm3/loadcell.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace MWRender
{
    ShoreDistanceMap::ShoreDistanceMap(ESMTerrain::Storage* terrainStorage)
        : mTerrainStorage(terrainStorage)
        , mMinX(0), mMinY(0), mMaxX(0), mMaxY(0)
        , mResolution(1024)
        , mMaxShoreDistance(2000.0f)  // 2000 MW units (~28 meters)
        , mGenerated(false)
    {
        // Create texture with default settings
        mTexture = new osg::Texture2D;
        mTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        mTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        mTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        mTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    }

    ShoreDistanceMap::~ShoreDistanceMap() = default;

    float ShoreDistanceMap::getTerrainHeight(float worldX, float worldY) const
    {
        if (!mTerrainStorage)
            return -1000.0f; // Deep water if no terrain

        // Use the default worldspace (Morrowind exterior)
        return mTerrainStorage->getHeightAt(osg::Vec3f(worldX, worldY, 0), ESM::Cell::sDefaultWorldspaceId);
    }

    void ShoreDistanceMap::generate(float minX, float minY, float maxX, float maxY, float waterLevel)
    {
        std::cout << "ShoreDistanceMap: Generating " << mResolution << "x" << mResolution
                  << " texture for world bounds [" << minX << "," << minY << "] to ["
                  << maxX << "," << maxY << "]" << std::endl;

        mMinX = minX;
        mMinY = minY;
        mMaxX = maxX;
        mMaxY = maxY;

        const int width = mResolution;
        const int height = mResolution;
        const float worldWidth = maxX - minX;
        const float worldHeight = maxY - minY;
        const float texelSizeX = worldWidth / width;
        const float texelSizeY = worldHeight / height;

        // Step 1: Sample terrain heights and create initial distance field
        // Land = 0, Water = infinity (we'll compute actual distance)
        std::vector<float> distances(width * height);
        std::vector<osg::Vec2i> nearestLand(width * height);

        const float INF = 1e10f;

        int landCount = 0;
        int waterCount = 0;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                float worldX = minX + (x + 0.5f) * texelSizeX;
                float worldY = minY + (y + 0.5f) * texelSizeY;

                float terrainHeight = getTerrainHeight(worldX, worldY);
                int idx = y * width + x;

                if (terrainHeight > waterLevel)
                {
                    // This is land - distance is 0
                    distances[idx] = 0.0f;
                    nearestLand[idx] = osg::Vec2i(x, y);
                    landCount++;
                }
                else
                {
                    // This is water - distance unknown
                    distances[idx] = INF;
                    nearestLand[idx] = osg::Vec2i(-1, -1);
                    waterCount++;
                }
            }
        }

        std::cout << "ShoreDistanceMap: Found " << landCount << " land texels, "
                  << waterCount << " water texels" << std::endl;

        // Step 2: Jump Flooding Algorithm to compute distance field
        // This is O(n log n) instead of O(n²) for brute force
        computeDistanceField(distances, width, height);

        // Step 3: Convert to texture
        // Scale distances to [0, 1] range based on maxShoreDistance
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(width, height, 1, GL_RED, GL_FLOAT);

        float* pixels = reinterpret_cast<float*>(image->data());
        for (int i = 0; i < width * height; ++i)
        {
            // Convert pixel distance to world distance
            float pixelDist = distances[i];
            float worldDist = pixelDist * texelSizeX; // Assuming square texels

            // Normalize to [0, 1] range, clamped at maxShoreDistance
            // 0 = on shore, 1 = far from shore (open ocean)
            pixels[i] = std::min(worldDist / mMaxShoreDistance, 1.0f);
        }

        mTexture->setImage(image);
        // Force GPU texture re-upload when image data changes
        mTexture->dirtyTextureObject();
        mGenerated = true;

        std::cout << "ShoreDistanceMap: Generation complete (maxDistance=" << mMaxShoreDistance << ")" << std::endl;
    }

    void ShoreDistanceMap::computeDistanceField(std::vector<float>& distances, int width, int height)
    {
        // Jump Flooding Algorithm (JFA)
        // Efficiently computes approximate distance field
        // Reference: https://www.comp.nus.edu.sg/~tants/jfa.html

        const float INF = 1e10f;

        // Store nearest seed (land pixel) for each pixel
        std::vector<osg::Vec2i> nearest(width * height);

        // Initialize: land pixels point to themselves, water to invalid
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int idx = y * width + x;
                if (distances[idx] < INF * 0.5f)
                {
                    nearest[idx] = osg::Vec2i(x, y);
                }
                else
                {
                    nearest[idx] = osg::Vec2i(-1, -1);
                }
            }
        }

        // JFA passes: step sizes are n/2, n/4, n/8, ..., 1
        int maxDim = std::max(width, height);
        for (int step = maxDim / 2; step >= 1; step /= 2)
        {
            std::vector<osg::Vec2i> newNearest = nearest;

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    int idx = y * width + x;

                    // Check 8 neighbors at distance 'step'
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            int nx = x + dx * step;
                            int ny = y + dy * step;

                            if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                                continue;

                            int nidx = ny * width + nx;
                            osg::Vec2i neighborSeed = nearest[nidx];

                            if (neighborSeed.x() < 0)
                                continue; // Neighbor has no seed

                            // Calculate distance to neighbor's seed
                            float distX = x - neighborSeed.x();
                            float distY = y - neighborSeed.y();
                            float dist = std::sqrt(distX * distX + distY * distY);

                            // If this is closer than current, update
                            osg::Vec2i currentSeed = newNearest[idx];
                            if (currentSeed.x() < 0)
                            {
                                // No current seed, use this one
                                newNearest[idx] = neighborSeed;
                            }
                            else
                            {
                                float currentDistX = x - currentSeed.x();
                                float currentDistY = y - currentSeed.y();
                                float currentDist = std::sqrt(currentDistX * currentDistX +
                                                              currentDistY * currentDistY);
                                if (dist < currentDist)
                                {
                                    newNearest[idx] = neighborSeed;
                                }
                            }
                        }
                    }
                }
            }

            nearest = std::move(newNearest);
        }

        // Final pass: convert nearest seed to distance
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int idx = y * width + x;
                osg::Vec2i seed = nearest[idx];

                if (seed.x() < 0)
                {
                    distances[idx] = INF; // No land found (shouldn't happen for valid maps)
                }
                else
                {
                    float distX = x - seed.x();
                    float distY = y - seed.y();
                    distances[idx] = std::sqrt(distX * distX + distY * distY);
                }
            }
        }
    }

    void ShoreDistanceMap::updateRegion(float minX, float minY, float maxX, float maxY, float waterLevel)
    {
        // For now, just regenerate the whole map
        // TODO: Implement partial update for streaming
        generate(mMinX, mMinY, mMaxX, mMaxY, waterLevel);
    }

    void ShoreDistanceMap::getWorldBounds(float& minX, float& minY, float& maxX, float& maxY) const
    {
        minX = mMinX;
        minY = mMinY;
        maxX = mMaxX;
        maxY = mMaxY;
    }

    osg::Vec2f ShoreDistanceMap::worldToUV(float worldX, float worldY) const
    {
        float u = (worldX - mMinX) / (mMaxX - mMinX);
        float v = (worldY - mMinY) / (mMaxY - mMinY);
        return osg::Vec2f(u, v);
    }
}
