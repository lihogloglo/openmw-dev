#ifndef OPENMW_MWRENDER_LAKE_H
#define OPENMW_MWRENDER_LAKE_H

#include "waterbody.hpp"

#include <osg/ref_ptr>
#include <osg/Vec3f>
#include <osg/Callback>
#include <map>
#include <cmath>

namespace osg
{
    class Geometry;
    class Group;
    class PositionAttitudeTransform;
    class StateSet;
    class Texture2D;
    class TextureCubeMap;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    // ============================================================================
    // MORROWIND UNIT SYSTEM
    // ============================================================================
    // In Morrowind: 22.1 units = 1 foot (from game engine documentation)
    // This means: 1 unit ≈ 0.0453 feet ≈ 0.0138 meters ≈ 1.38 cm
    // Cell size: 8192 units = ~370.5 feet = ~113 meters
    // ============================================================================
    namespace Units
    {
        // Base conversion constants
        constexpr float UNITS_PER_FOOT = 22.1f;
        constexpr float FEET_PER_UNIT = 1.0f / 22.1f;
        constexpr float UNITS_PER_METER = UNITS_PER_FOOT / 0.3048f;  // ~72.53
        constexpr float METERS_PER_UNIT = 1.0f / UNITS_PER_METER;    // ~0.0138

        // Cell dimensions
        constexpr float CELL_SIZE_UNITS = 8192.0f;
        constexpr float CELL_SIZE_FEET = CELL_SIZE_UNITS * FEET_PER_UNIT;      // ~370.5 feet
        constexpr float CELL_SIZE_METERS = CELL_SIZE_UNITS * METERS_PER_UNIT;  // ~113 meters

        // Validation bounds (Morrowind world is roughly -130k to +130k in each axis)
        constexpr float MAX_WORLD_COORD = 300000.0f;   // Conservative maximum coordinate
        constexpr float MIN_WORLD_COORD = -300000.0f;  // Conservative minimum coordinate
        constexpr float MAX_ALTITUDE = 10000.0f;       // Maximum height in MW (Red Mountain)
        constexpr float MIN_ALTITUDE = -5000.0f;       // Minimum depth (underwater areas)

        // Validation functions
        inline bool isValidWorldPos(float x, float y)
        {
            return x >= MIN_WORLD_COORD && x <= MAX_WORLD_COORD &&
                   y >= MIN_WORLD_COORD && y <= MAX_WORLD_COORD;
        }

        inline bool isValidHeight(float h)
        {
            return h >= MIN_ALTITUDE && h <= MAX_ALTITUDE;
        }

        // World position to grid cell conversion
        inline void worldToGrid(float worldX, float worldY, int& gridX, int& gridY)
        {
            gridX = static_cast<int>(std::floor(worldX / CELL_SIZE_UNITS));
            gridY = static_cast<int>(std::floor(worldY / CELL_SIZE_UNITS));
        }

        // Grid cell to world position (cell center)
        inline void gridToWorld(int gridX, int gridY, float& worldX, float& worldY)
        {
            worldX = gridX * CELL_SIZE_UNITS + CELL_SIZE_UNITS * 0.5f;
            worldY = gridY * CELL_SIZE_UNITS + CELL_SIZE_UNITS * 0.5f;
        }
    }

    class WaterManager;

    class Lake : public WaterBody
    {
    public:
        Lake(osg::Group* parent, Resource::ResourceSystem* resourceSystem);
        ~Lake() override;

        void setEnabled(bool enabled) override;
        void update(float dt, bool paused, const osg::Vec3f& cameraPos) override;
        void setHeight(float height) override;  // Sets default height only
        bool isUnderwater(const osg::Vec3f& pos) const override;

        void addToScene(osg::Group* parent) override;
        void removeFromScene(osg::Group* parent) override;

        // Per-cell water management
        void addWaterCell(int gridX, int gridY, float height, const osg::Vec3f& waterColor = osg::Vec3f(0.15f, 0.25f, 0.35f));
        void removeWaterCell(int gridX, int gridY);
        void clearAllCells();
        float getWaterHeightAt(const osg::Vec3f& pos) const;
        osg::Vec3f getWaterColorAt(const osg::Vec3f& pos) const;

        // Per-cell visibility control (for integration with cell loading)
        void showWaterCell(int gridX, int gridY);
        void hideWaterCell(int gridX, int gridY);

        // Reflection system integration (inline SSR + cubemap fallback)
        void setWaterManager(WaterManager* waterManager);

        // Debug control
        // Debug modes:
        // 0 = Normal rendering (SSR + cubemap + water color)
        // 1 = Solid color (verify geometry is rendering) - MAGENTA
        // 2 = World position visualization (RGB = XYZ) - should NOT change with camera
        // 3 = Normal visualization (animated wave normals)
        // 4 = SSR only (no cubemap fallback) - screen-space reflections
        // 5 = Cubemap only (no SSR) - environment map reflections
        // 6 = SSR confidence visualization (green = high confidence)
        // 7 = Screen UV visualization (RG = screen coordinates)
        // 8 = Depth visualization (linear depth, grayscale)
        // 9 = Emergency fallback (simple water color, no reflections)
        // 10 = Fragment depth value (grayscale, raw gl_FragCoord.z)
        // 11 = Near/far visualization (blue=near, red=far, for reversed-Z)
        // 12 = Depth range indicator (green=near, yellow=mid, red=far)
        void setDebugMode(int mode);
        int getDebugMode() const;

        // Get lake cell count for debugging
        size_t getCellCount() const { return mCellWaters.size(); }

    private:
        struct CellWater
        {
            int gridX, gridY;
            float height;
            osg::Vec3f waterColor;
            osg::ref_ptr<osg::Group> transform;
            osg::ref_ptr<osg::Geometry> geometry;
        };

        void createCellGeometry(CellWater& cell);
        osg::ref_ptr<osg::StateSet> createWaterStateSet();

        osg::ref_ptr<osg::Group> mParent;
        Resource::ResourceSystem* mResourceSystem;
        WaterManager* mWaterManager;  // For SSR/cubemap access (non-owning)

        osg::ref_ptr<osg::PositionAttitudeTransform> mRootNode;
        std::map<std::pair<int, int>, CellWater> mCellWaters;
        osg::ref_ptr<osg::StateSet> mWaterStateSet;
        osg::ref_ptr<osg::Callback> mStateSetUpdater;


        bool mEnabled;
    };
}

#endif
