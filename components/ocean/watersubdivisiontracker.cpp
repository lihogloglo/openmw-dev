#include "watersubdivisiontracker.hpp"

#include <cmath>

namespace Ocean
{
    WaterSubdivisionTracker::WaterSubdivisionTracker()
        : mNearDistance(12000.0f)  // Increased to cover immediate 8km chunk + neighbors
        , mMidDistance(20000.0f)
        , mFarDistance(40000.0f)
        , mPlayerPosition(0.0f, 0.0f)
    {
    }

    void WaterSubdivisionTracker::update(const osg::Vec2f& playerPos)
    {
        mPlayerPosition = playerPos;

        // Update subdivision levels for all tracked chunks based on new player position
        for (auto& pair : mTrackedChunks)
        {
            ChunkSubdivisionData& data = pair.second;
            float distance = (data.chunkCenter - mPlayerPosition).length();
            data.subdivisionLevel = calculateSubdivisionLevel(distance);
        }
    }

    int WaterSubdivisionTracker::getSubdivisionLevel(const osg::Vec2f& chunkCenter, float distance) const
    {
        // Check if this chunk is already tracked
        auto key = chunkToKey(chunkCenter);
        auto it = mTrackedChunks.find(key);

        if (it != mTrackedChunks.end())
        {
            return it->second.subdivisionLevel;
        }

        // Not tracked yet, calculate level based on distance
        return calculateSubdivisionLevel(distance);
    }

    void WaterSubdivisionTracker::markChunkSubdivided(const osg::Vec2f& chunkCenter, int level)
    {
        auto key = chunkToKey(chunkCenter);
        ChunkSubdivisionData& data = mTrackedChunks[key];
        data.chunkCenter = chunkCenter;
        data.subdivisionLevel = level;
    }

    void WaterSubdivisionTracker::clear()
    {
        mTrackedChunks.clear();
    }

    std::pair<int, int> WaterSubdivisionTracker::chunkToKey(const osg::Vec2f& center) const
    {
        // Round to nearest integer to handle floating point precision
        return std::make_pair(
            static_cast<int>(std::round(center.x())),
            static_cast<int>(std::round(center.y()))
        );
    }

    int WaterSubdivisionTracker::calculateSubdivisionLevel(float distance) const
    {
        // Based on distance thresholds from the design document:
        // Level 3 (highest detail): < 512m
        // Level 2 (medium detail): 512-1536m
        // Level 1 (low detail): 1536-4096m
        // Level 0 (no subdivision): > 4096m

        if (distance < mNearDistance)
            return 3;
        else if (distance < mMidDistance)
            return 2;
        else if (distance < mFarDistance)
            return 1;
        else
            return 0;
    }
}
