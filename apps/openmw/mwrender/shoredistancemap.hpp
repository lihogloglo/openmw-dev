#ifndef OPENMW_MWRENDER_SHOREDISTANCEMAP_H
#define OPENMW_MWRENDER_SHOREDISTANCEMAP_H

#include <osg/Texture2D>
#include <osg/Vec2f>
#include <osg/ref_ptr>

namespace ESMTerrain
{
    class Storage;
}

namespace MWRender
{
    /**
     * Generates a world-space texture encoding distance to shore/land.
     *
     * This texture is sampled by the ocean vertex shader to attenuate
     * wave displacement near coastlines.
     *
     * Format: R16F (16-bit float)
     * Value: Distance to nearest land in MW units (0 = on land, positive = in water)
     *
     * Generation is done once at initialization using jump flooding algorithm.
     */
    class ShoreDistanceMap
    {
    public:
        ShoreDistanceMap(ESMTerrain::Storage* terrainStorage);
        ~ShoreDistanceMap();

        /**
         * Generate the shore distance texture for the given world bounds.
         *
         * @param minX Minimum world X coordinate
         * @param minY Minimum world Y coordinate
         * @param maxX Maximum world X coordinate
         * @param maxY Maximum world Y coordinate
         * @param waterLevel The sea level height (typically 0)
         */
        void generate(float minX, float minY, float maxX, float maxY, float waterLevel = 0.0f);

        /**
         * Update a region of the map (for streaming/cell loading).
         * More efficient than regenerating the entire map.
         */
        void updateRegion(float minX, float minY, float maxX, float maxY, float waterLevel = 0.0f);

        /// Get the generated texture for binding to shaders
        osg::Texture2D* getTexture() { return mTexture.get(); }

        /// Get the world bounds this map covers
        void getWorldBounds(float& minX, float& minY, float& maxX, float& maxY) const;

        /// Convert world position to UV coordinates for texture sampling
        osg::Vec2f worldToUV(float worldX, float worldY) const;

        /// Get texture resolution
        int getResolution() const { return mResolution; }

        /// Check if the map has been generated
        bool isGenerated() const { return mGenerated; }

        // Configuration
        void setResolution(int resolution) { mResolution = resolution; }
        void setMaxShoreDistance(float distance) { mMaxShoreDistance = distance; }

    private:
        /// Sample terrain height at world position
        float getTerrainHeight(float worldX, float worldY) const;

        /// Run jump flooding algorithm to compute distance field
        void computeDistanceField(std::vector<float>& distances, int width, int height);

        ESMTerrain::Storage* mTerrainStorage;
        osg::ref_ptr<osg::Texture2D> mTexture;

        // World bounds
        float mMinX, mMinY, mMaxX, mMaxY;

        // Configuration
        int mResolution;        // Texture size (default 1024)
        float mMaxShoreDistance; // Max distance to encode (default 2000 MW units)
        bool mGenerated;
    };
}

#endif
