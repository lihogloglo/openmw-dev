#include "watertypeclassifier.hpp"

#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>

#include "../mwworld/cellstore.hpp"

#include <algorithm>
#include <queue>

namespace MWRender
{
    WaterTypeClassifier::WaterTypeClassifier()
    {
    }

    Ocean::WaterType WaterTypeClassifier::classifyCell(const MWWorld::CellStore* cell) const
    {
        if (!cell)
            return Ocean::WaterType::INDOOR;

        const MWWorld::Cell* cellData = cell->getCell();
        if (!cellData)
            return Ocean::WaterType::INDOOR;

        // Interior cells always have static indoor water
        if (!cellData->isExterior())
            return Ocean::WaterType::INDOOR;

        // Check if cell has water at all
        if (!cellData->hasWater())
            return Ocean::WaterType::POND; // Treat as pond (will not be rendered anyway)

        // Check cache first
        auto it = mClassificationCache.find(cell);
        if (it != mClassificationCache.end())
            return it->second;

        // Perform classification
        std::set<osg::Vec2i> visited;

        // Check if connected to ocean (world edge)
        bool isOcean = isConnectedToWorldEdge(cell, visited);
        if (isOcean)
        {
            mClassificationCache[cell] = Ocean::WaterType::OCEAN;
            return Ocean::WaterType::OCEAN;
        }

        // Count connected water cells for lake classification
        visited.clear();
        int waterCellCount = countConnectedWaterCells(cell, visited);

        Ocean::WaterType type;
        if (waterCellCount > 100)
            type = Ocean::WaterType::LARGE_LAKE;
        else if (waterCellCount > 10)
            type = Ocean::WaterType::SMALL_LAKE;
        else
            type = Ocean::WaterType::POND;

        mClassificationCache[cell] = type;
        return type;
    }

    void WaterTypeClassifier::preClassifyRegion(const std::vector<const MWWorld::CellStore*>& cells)
    {
        for (const auto* cell : cells)
        {
            if (cell)
                classifyCell(cell);
        }
    }

    void WaterTypeClassifier::clearCache()
    {
        mClassificationCache.clear();
    }

    Ocean::WaterType WaterTypeClassifier::getCachedType(const MWWorld::CellStore* cell) const
    {
        auto it = mClassificationCache.find(cell);
        if (it != mClassificationCache.end())
            return it->second;
        return Ocean::WaterType::INDOOR;
    }

    bool WaterTypeClassifier::isConnectedToWorldEdge(const MWWorld::CellStore* cell,
        std::set<osg::Vec2i>& visited,
        int maxDepth) const
    {
        if (!cell || !cell->getCell() || !cell->getCell()->isExterior())
            return false;

        // Morrowind world boundaries (approximate)
        // The game world is roughly from -30 to +30 in both X and Y
        const int WORLD_MIN = -35;
        const int WORLD_MAX = 35;

        std::queue<osg::Vec2i> toVisit;
        osg::Vec2i startCoords(cell->getCell()->getGridX(), cell->getCell()->getGridY());

        toVisit.push(startCoords);
        visited.insert(startCoords);

        int depth = 0;
        while (!toVisit.empty() && depth < maxDepth)
        {
            int levelSize = static_cast<int>(toVisit.size());
            for (int i = 0; i < levelSize; ++i)
            {
                osg::Vec2i current = toVisit.front();
                toVisit.pop();

                // Check if at world edge
                if (current.x() <= WORLD_MIN || current.x() >= WORLD_MAX ||
                    current.y() <= WORLD_MIN || current.y() >= WORLD_MAX)
                {
                    return true; // Connected to ocean
                }

                // Add water neighbors
                auto neighbors = getWaterNeighbors(current);
                for (const auto& neighbor : neighbors)
                {
                    if (visited.find(neighbor) == visited.end())
                    {
                        visited.insert(neighbor);
                        toVisit.push(neighbor);
                    }
                }
            }
            depth++;
        }

        return false;
    }

    int WaterTypeClassifier::countConnectedWaterCells(const MWWorld::CellStore* cell,
        std::set<osg::Vec2i>& visited,
        int maxCount) const
    {
        if (!cell || !cell->getCell() || !cell->getCell()->isExterior())
            return 0;

        std::queue<osg::Vec2i> toVisit;
        osg::Vec2i startCoords(cell->getCell()->getGridX(), cell->getCell()->getGridY());

        toVisit.push(startCoords);
        visited.insert(startCoords);

        int count = 1;

        while (!toVisit.empty() && count < maxCount)
        {
            osg::Vec2i current = toVisit.front();
            toVisit.pop();

            // Add water neighbors
            auto neighbors = getWaterNeighbors(current);
            for (const auto& neighbor : neighbors)
            {
                if (visited.find(neighbor) == visited.end())
                {
                    visited.insert(neighbor);
                    toVisit.push(neighbor);
                    count++;

                    if (count < maxCount)
                        toVisit.push(neighbor);
                }
            }
        }

        return count;
    }

    bool WaterTypeClassifier::cellHasWater(const MWWorld::CellStore* cell) const
    {
        if (!cell || !cell->getCell())
            return false;

        const MWWorld::Cell* cellData = cell->getCell();
        return cellData->hasWater() && cellData->isExterior();
    }

    std::vector<osg::Vec2i> WaterTypeClassifier::getWaterNeighbors(osg::Vec2i cellCoords) const
    {
        std::vector<osg::Vec2i> neighbors;

        // 4-directional neighbors (N, S, E, W)
        const osg::Vec2i offsets[] = {
            osg::Vec2i(0, 1),   // North
            osg::Vec2i(0, -1),  // South
            osg::Vec2i(1, 0),   // East
            osg::Vec2i(-1, 0)   // West
        };

        for (const auto& offset : offsets)
        {
            neighbors.push_back(cellCoords + offset);
        }

        return neighbors;
    }
}
