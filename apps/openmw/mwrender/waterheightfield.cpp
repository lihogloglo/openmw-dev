#include "waterheightfield.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>

#include <components/misc/constants.hpp>

#include "../mwworld/cellstore.hpp"

namespace MWRender
{
    // Explicit lake overrides - inland water bodies that should NEVER show ocean
    // This catches special cases that grid-based detection might miss
    static const std::set<std::string> KNOWN_LAKE_CELLS = {
        // Vivec canals - These are canals between cantons, not open ocean
        "Vivec, Arena",
        "Vivec, Temple",
        "Vivec, Foreign Quarter",
        "Vivec, Hlaalu",
        "Vivec, Redoran",
        "Vivec, Telvanni",
        "Vivec, St. Delyn",
        "Vivec, St. Olms",

        // Balmora - Odai River runs through the city
        "Balmora",

        // Other settlements with rivers/lakes
        "Ebonheart",
        "Ebonheart, Imperial Chapels",
        "Sadrith Mora",
        "Tel Branora",
        "Wolverine Hall",

        // Caldera - Crater lake
        "Caldera",

        // Seyda Neen - Has both ocean AND inland water, but default to lake
        // to prevent ocean from appearing in the river
        "Seyda Neen",
    };

    WaterHeightField::WaterHeightField(int resolution, float texelsPerMWUnit)
        : mSize(resolution)
        , mTexelsPerUnit(texelsPerMWUnit)
        , mOrigin(0, 0)
    {
        // Create height field texture (R16F format)
        mHeightField = new osg::Image;
        mHeightField->allocateImage(mSize, mSize, 1, GL_RED, GL_FLOAT);

        // Create water type texture (R8UI format)
        mWaterType = new osg::Image;
        mWaterType->allocateImage(mSize, mSize, 1, GL_RED, GL_UNSIGNED_BYTE);

        // Initialize to "no water"
        std::fill_n(reinterpret_cast<float*>(mHeightField->data()), mSize * mSize, NO_WATER_HEIGHT);
        std::fill_n(reinterpret_cast<uint8_t*>(mWaterType->data()), mSize * mSize,
            static_cast<uint8_t>(WaterType::None));
    }

    void WaterHeightField::updateFromLoadedCells(const std::vector<const MWWorld::CellStore*>& cells)
    {
        if (cells.empty())
            return;

        // Calculate bounding box of loaded cells
        osg::Vec2i minGrid(INT_MAX, INT_MAX);
        osg::Vec2i maxGrid(INT_MIN, INT_MIN);

        for (const auto* cell : cells)
        {
            if (!cell->getCell()->isExterior())
                continue;

            int x = cell->getCell()->getGridX();
            int y = cell->getCell()->getGridY();

            minGrid.x() = std::min(minGrid.x(), x);
            minGrid.y() = std::min(minGrid.y(), y);
            maxGrid.x() = std::max(maxGrid.x(), x);
            maxGrid.y() = std::max(maxGrid.y(), y);
        }

        // Center texture on loaded area
        mOrigin.x() = (minGrid.x() + maxGrid.x()) / 2;
        mOrigin.y() = (minGrid.y() + maxGrid.y()) / 2;

        // Clear texture to "no water"
        std::fill_n(reinterpret_cast<float*>(mHeightField->data()), mSize * mSize, NO_WATER_HEIGHT);
        std::fill_n(reinterpret_cast<uint8_t*>(mWaterType->data()), mSize * mSize,
            static_cast<uint8_t>(WaterType::None));

        // Rasterize each cell
        for (const auto* cell : cells)
        {
            if (!cell->getCell()->hasWater())
                continue;

            float waterHeight = cell->getCell()->getWaterHeight();
            WaterType waterType = classifyWaterType(cell);

            rasterizeCell(cell, waterHeight, waterType);
        }

        // Mark textures dirty for GPU upload
        mHeightField->dirty();
        mWaterType->dirty();
    }

    float WaterHeightField::sampleHeight(const osg::Vec3f& worldPos) const
    {
        osg::Vec2f uv = worldToUV(osg::Vec2f(worldPos.x(), worldPos.y()));

        // Convert UV to texel coordinates
        int x = static_cast<int>(uv.x() * mSize);
        int y = static_cast<int>(uv.y() * mSize);

        // Bounds check
        if (x < 0 || x >= mSize || y < 0 || y >= mSize)
            return NO_WATER_HEIGHT;

        // Sample texture
        const float* data = reinterpret_cast<const float*>(mHeightField->data());
        return data[y * mSize + x];
    }

    WaterType WaterHeightField::sampleType(const osg::Vec3f& worldPos) const
    {
        osg::Vec2f uv = worldToUV(osg::Vec2f(worldPos.x(), worldPos.y()));

        int x = static_cast<int>(uv.x() * mSize);
        int y = static_cast<int>(uv.y() * mSize);

        if (x < 0 || x >= mSize || y < 0 || y >= mSize)
            return WaterType::None;

        const uint8_t* data = reinterpret_cast<const uint8_t*>(mWaterType->data());
        return static_cast<WaterType>(data[y * mSize + x]);
    }

    osg::Image* WaterHeightField::generateOceanMask()
    {
        // Create mask texture if it doesn't exist
        if (!mOceanMask)
        {
            mOceanMask = new osg::Image;
            mOceanMask->allocateImage(mSize, mSize, 1, GL_RED, GL_UNSIGNED_BYTE);
        }

        // Generate mask from water type texture
        // 255 (1.0) = Ocean, 0 (0.0) = Lake/River/None
        const uint8_t* typeData = reinterpret_cast<const uint8_t*>(mWaterType->data());
        uint8_t* maskData = reinterpret_cast<uint8_t*>(mOceanMask->data());

        for (int i = 0; i < mSize * mSize; ++i)
        {
            WaterType type = static_cast<WaterType>(typeData[i]);
            maskData[i] = (type == WaterType::Ocean) ? 255 : 0;
        }

        mOceanMask->dirty();
        return mOceanMask.get();
    }

    osg::Vec2f WaterHeightField::worldToUV(const osg::Vec2f& worldPos) const
    {
        // Convert world position to grid coordinates
        const float cellSize = Constants::CellSizeInUnits;  // 8192 MW units
        osg::Vec2f gridPos(worldPos.x() / cellSize, worldPos.y() / cellSize);

        // Offset by texture origin
        gridPos.x() -= mOrigin.x();
        gridPos.y() -= mOrigin.y();

        // Convert to texture coordinates (centered)
        float halfCoverage = (mSize / mTexelsPerUnit) / cellSize / 2.0f;
        osg::Vec2f uv(
            (gridPos.x() + halfCoverage) / (2.0f * halfCoverage), (gridPos.y() + halfCoverage) / (2.0f * halfCoverage));

        return uv;
    }

    osg::Vec2i WaterHeightField::worldGridToTexel(const osg::Vec2i& gridPos) const
    {
        // Offset by origin
        osg::Vec2i relative(gridPos.x() - mOrigin.x(), gridPos.y() - mOrigin.y());

        // Convert to texel coordinates
        const float cellSize = Constants::CellSizeInUnits;
        float texelsPerCell = cellSize * mTexelsPerUnit;

        int halfSize = mSize / 2;

        osg::Vec2i texel(static_cast<int>(relative.x() * texelsPerCell) + halfSize,
            static_cast<int>(relative.y() * texelsPerCell) + halfSize);

        return texel;
    }

    void WaterHeightField::rasterizeCell(
        const MWWorld::CellStore* cell, float waterHeight, WaterType waterType)
    {
        const float cellSize = Constants::CellSizeInUnits;

        if (!cell->getCell()->isExterior())
        {
            // Interior cells: Just mark center texel
            // (Interior water is not spatially tracked in height field)
            return;
        }

        // Get cell grid position
        osg::Vec2i gridPos(cell->getCell()->getGridX(), cell->getCell()->getGridY());

        // Convert to texel bounds
        osg::Vec2i texelMin = worldGridToTexel(gridPos);
        osg::Vec2i texelMax = worldGridToTexel(osg::Vec2i(gridPos.x() + 1, gridPos.y() + 1));

        // Clamp to texture bounds
        texelMin.x() = std::max(0, texelMin.x());
        texelMin.y() = std::max(0, texelMin.y());
        texelMax.x() = std::min(mSize, texelMax.x());
        texelMax.y() = std::min(mSize, texelMax.y());

        // Rasterize cell bounds into texture
        float* heightData = reinterpret_cast<float*>(mHeightField->data());
        uint8_t* typeData = reinterpret_cast<uint8_t*>(mWaterType->data());

        for (int y = texelMin.y(); y < texelMax.y(); ++y)
        {
            for (int x = texelMin.x(); x < texelMax.x(); ++x)
            {
                int index = y * mSize + x;
                heightData[index] = waterHeight;
                typeData[index] = static_cast<uint8_t>(waterType);
            }
        }
    }

    WaterType WaterHeightField::classifyWaterType(const MWWorld::CellStore* cell) const
    {
        // Interior cells = always lakes
        if (!cell->getCell()->isExterior())
            return WaterType::Lake;

        float waterHeight = cell->getCell()->getWaterHeight();

        // High altitude water = definitely lake (mountains, etc.)
        if (waterHeight > 50.0f || waterHeight < -50.0f)
            return WaterType::Lake;

        // Get cell grid coordinates for geographical classification
        int gridX = cell->getCell()->getGridX();
        int gridY = cell->getCell()->getGridY();

        // Check manual override list first (Vivec, Balmora, etc.)
        if (isKnownLake(cell))
            return WaterType::Lake;

        // Grid-based ocean detection for Vvardenfell
        // Based on actual Morrowind coastline geography

        // Far west ocean (beyond West Gash)
        if (gridX < -22)
            return WaterType::Ocean;

        // Far east ocean (beyond Molag Amur)
        if (gridX > 27)
            return WaterType::Ocean;

        // Far north ocean (Sheogorad region)
        if (gridY > 27)
            return WaterType::Ocean;

        // Far south ocean (beyond Molag Mar)
        if (gridY < -28)
            return WaterType::Ocean;

        // Azura's Coast - East coast region
        // Roughly from Tel Fyr to Sadrith Mora area
        if (gridX > 16 && gridY > -12 && gridY < 18)
            return WaterType::Ocean;

        // West Gash coastal region
        // Western coast from Gnaar Mok to Ald Velothi
        if (gridX < -12 && gridY > -18 && gridY < 12)
            return WaterType::Ocean;

        // Bitter Coast - Southwest region
        // South of Balmora, west of Vivec
        if (gridY < -12 && gridX < -4 && gridX > -20)
            return WaterType::Ocean;

        // South coast near Vivec/Suran
        // Careful to exclude Vivec canals (handled by known lakes list)
        if (gridY < -16 && gridX > -4 && gridX < 12)
            return WaterType::Ocean;

        // Everything else inland = Lake
        // This includes: Vivec canals, Balmora river, interior lakes, ponds
        return WaterType::Lake;
    }

    bool WaterHeightField::isPerimeterCell(const MWWorld::CellStore* cell) const
    {
        if (!cell->getCell()->isExterior())
            return false;

        int x = cell->getCell()->getGridX();
        int y = cell->getCell()->getGridY();

        // Morrowind world roughly spans -30 to +30
        // Ocean cells are typically at the edges
        const int PERIMETER_THRESHOLD = 25;

        return (std::abs(x) > PERIMETER_THRESHOLD || std::abs(y) > PERIMETER_THRESHOLD);
    }

    bool WaterHeightField::isKnownLake(const MWWorld::CellStore* cell) const
    {
        if (!cell->getCell()->isExterior())
            return true; // All interior water is lakes

        std::string cellName(cell->getCell()->getNameId());

        // Check full name match
        if (KNOWN_LAKE_CELLS.count(cellName))
            return true;

        // Check partial matches (e.g., "Vivec" in cell name)
        for (const auto& lakeName : KNOWN_LAKE_CELLS)
        {
            if (cellName.find(lakeName) != std::string::npos)
                return true;
        }

        return false;
    }
}
