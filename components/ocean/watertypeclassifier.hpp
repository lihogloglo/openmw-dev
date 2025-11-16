#ifndef OPENMW_COMPONENTS_OCEAN_WATERTYPECLASSIFIER_H
#define OPENMW_COMPONENTS_OCEAN_WATERTYPECLASSIFIER_H

#include "watertype.hpp"

#include <map>
#include <set>
#include <vector>

#include <osg/Vec2i>

namespace ESM
{
    struct Cell;
}

namespace MWWorld
{
    class CellStore;
}

namespace Ocean
{
    /// Automatic water type detection based on world data
    /// Implements Method 2 from the ocean implementation design document
    class WaterTypeClassifier
    {
    public:
        WaterTypeClassifier();

        /// Classify water type for a given cell
        /// This uses automatic detection based on cell properties and neighboring cells
        WaterType classifyCell(const MWWorld::CellStore* cell) const;

        /// Pre-classify cells in a region for performance
        /// Call this when loading a new region to cache water type classifications
        void preClassifyRegion(const std::vector<const MWWorld::CellStore*>& cells);

        /// Clear cached classifications
        void clearCache();

        /// Get cached water type for a cell (returns INDOOR if not cached)
        WaterType getCachedType(const MWWorld::CellStore* cell) const;

    private:
        /// Check if a cell is connected to the world edge (ocean detection)
        bool isConnectedToWorldEdge(const MWWorld::CellStore* cell,
            std::set<osg::Vec2i>& visited,
            int maxDepth = 50) const;

        /// Count connected water cells using flood fill
        int countConnectedWaterCells(const MWWorld::CellStore* cell,
            std::set<osg::Vec2i>& visited,
            int maxCount = 200) const;

        /// Check if a cell has water
        bool cellHasWater(const MWWorld::CellStore* cell) const;

        /// Get neighboring cells with water
        std::vector<osg::Vec2i> getWaterNeighbors(osg::Vec2i cellCoords) const;

        /// Cache of classified cells
        mutable std::map<const MWWorld::CellStore*, WaterType> mClassificationCache;
    };
}

#endif
