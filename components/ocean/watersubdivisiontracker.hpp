#ifndef OPENMW_COMPONENTS_OCEAN_WATERSUBDIVISIONTRACKER_H
#define OPENMW_COMPONENTS_OCEAN_WATERSUBDIVISIONTRACKER_H

#include <map>
#include <osg/Vec2f>

namespace Ocean
{
    /// Tracks which water chunks should be subdivided based on distance from player
    /// Adapted from Terrain::SubdivisionTracker for water use
    class WaterSubdivisionTracker
    {
    public:
        struct ChunkSubdivisionData
        {
            int subdivisionLevel;      // Current subdivision level (0-3)
            osg::Vec2f chunkCenter;    // Chunk position for distance calculations

            ChunkSubdivisionData()
                : subdivisionLevel(0)
                , chunkCenter(0.0f, 0.0f)
            {}
        };

        WaterSubdivisionTracker();

        /// Update tracker each frame
        /// @param playerPos Current player position in world space
        void update(const osg::Vec2f& playerPos);

        /// Get the subdivision level for a water chunk at given position
        /// @param chunkCenter Chunk center in world coordinates
        /// @param distance Distance from player to chunk center in world units
        /// @return Subdivision level (0-3)
        int getSubdivisionLevel(const osg::Vec2f& chunkCenter, float distance) const;

        /// Mark a chunk as subdivided (called when chunk is created with subdivision)
        /// @param chunkCenter Chunk center in world coordinates
        /// @param level Subdivision level applied
        void markChunkSubdivided(const osg::Vec2f& chunkCenter, int level);

        /// Clear all tracked chunks (call when changing cells/worldspaces)
        void clear();

        /// Get number of currently tracked chunks
        size_t getTrackedChunkCount() const { return mTrackedChunks.size(); }

        /// Configuration - set subdivision distance thresholds
        /// These determine how far from the player each subdivision level extends
        void setNearDistance(float distance) { mNearDistance = distance; }
        void setMidDistance(float distance) { mMidDistance = distance; }
        void setFarDistance(float distance) { mFarDistance = distance; }

        float getNearDistance() const { return mNearDistance; }
        float getMidDistance() const { return mMidDistance; }
        float getFarDistance() const { return mFarDistance; }

    private:
        /// Map of chunk centers to their subdivision data
        std::map<std::pair<int, int>, ChunkSubdivisionData> mTrackedChunks;

        /// Distance thresholds for subdivision levels (in world units)
        /// Level 3 (highest): distance < mNearDistance
        /// Level 2: mNearDistance <= distance < mMidDistance
        /// Level 1: mMidDistance <= distance < mFarDistance
        /// Level 0 (no subdivision): distance >= mFarDistance
        float mNearDistance;   // Default: 512.0
        float mMidDistance;    // Default: 1536.0
        float mFarDistance;    // Default: 4096.0

        /// Current player position (for distance calculations)
        osg::Vec2f mPlayerPosition;

        /// Convert chunk center to integer key for map lookup
        std::pair<int, int> chunkToKey(const osg::Vec2f& center) const;

        /// Calculate subdivision level based on distance from player
        int calculateSubdivisionLevel(float distance) const;
    };
}

#endif
