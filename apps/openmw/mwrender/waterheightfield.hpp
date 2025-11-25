#ifndef OPENMW_MWRENDER_WATERHEIGHTFIELD_HPP
#define OPENMW_MWRENDER_WATERHEIGHTFIELD_HPP

#include <osg/Image>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <vector>

namespace MWWorld
{
    class CellStore;
}

namespace MWRender
{
    /// Water type classification
    enum class WaterType : uint8_t
    {
        None = 0,
        Ocean = 1,
        Lake = 2,
        River = 3
    };

    /**
     * @brief 2D height field covering loaded cells for efficient water height queries
     *
     * This class maintains a texture-based representation of water heights across the world.
     * It's used for:
     * - Fast swimming detection (PhysicsSystem)
     * - Underwater effects (shaders)
     * - Multi-altitude water support
     *
     * Resolution: ~1 texel per 10 MW units (configurable)
     * Memory: 2048×2048 @ R16F + R8UI = ~12 MB
     */
    class WaterHeightField
    {
    public:
        /**
         * @param resolution Texture size (should be power of 2)
         * @param texelsPerMWUnit Spatial resolution (0.1 = 1 texel per 10 units)
         */
        explicit WaterHeightField(int resolution = 2048, float texelsPerMWUnit = 0.1f);

        /**
         * @brief Update height field from currently loaded cells
         * @param cells List of loaded cells
         *
         * Rasterizes water data from cells into the height field texture.
         * Called when cells are loaded/unloaded.
         */
        void updateFromLoadedCells(const std::vector<const MWWorld::CellStore*>& cells);

        /**
         * @brief Sample water height at world position
         * @param worldPos Position in world space
         * @return Water height at position, or -1000.0f if no water
         */
        float sampleHeight(const osg::Vec3f& worldPos) const;

        /**
         * @brief Sample water type at world position
         * @param worldPos Position in world space
         * @return Water type (None, Ocean, Lake, River)
         */
        WaterType sampleType(const osg::Vec3f& worldPos) const;

        /**
         * @brief Get height field texture (for shader binding)
         * @return R16F texture with water heights
         */
        osg::Image* getHeightTexture() { return mHeightField.get(); }
        const osg::Image* getHeightTexture() const { return mHeightField.get(); }

        /**
         * @brief Get water type texture (for shader binding)
         * @return R8UI texture with water types
         */
        osg::Image* getTypeTexture() { return mWaterType.get(); }
        const osg::Image* getTypeTexture() const { return mWaterType.get(); }

        /**
         * @brief Get texture origin in world grid coordinates
         */
        osg::Vec2i getOrigin() const { return mOrigin; }

        /**
         * @brief Get texels per MW unit (for shader uniforms)
         */
        float getTexelsPerUnit() const { return mTexelsPerUnit; }

        /**
         * @brief Get texture size
         */
        int getSize() const { return mSize; }

    private:
        osg::ref_ptr<osg::Image> mHeightField;  ///< R16F: Water height
        osg::ref_ptr<osg::Image> mWaterType;    ///< R8UI: Water type enum

        osg::Vec2i mOrigin;       ///< World grid coordinates of texture (0,0)
        int mSize;                ///< Texture dimensions (square)
        float mTexelsPerUnit;     ///< Spatial resolution

        static constexpr float NO_WATER_HEIGHT = -1000.0f;

        /**
         * @brief Convert world position to texture UV coordinates
         */
        osg::Vec2f worldToUV(const osg::Vec2f& worldPos) const;

        /**
         * @brief Convert world grid coordinates to texel coordinates
         */
        osg::Vec2i worldGridToTexel(const osg::Vec2i& gridPos) const;

        /**
         * @brief Rasterize a cell's water into the height field
         * @param cell Cell to rasterize
         * @param waterHeight Water height in cell
         * @param waterType Water type classification
         */
        void rasterizeCell(const MWWorld::CellStore* cell, float waterHeight, WaterType waterType);

        /**
         * @brief Classify water type for a cell
         * @param cell Cell to classify
         * @return Classified water type
         */
        WaterType classifyWaterType(const MWWorld::CellStore* cell) const;

        /**
         * @brief Check if cell is at world perimeter (likely ocean)
         */
        bool isPerimeterCell(const MWWorld::CellStore* cell) const;

        /**
         * @brief Check if cell is in the manual lake override list
         */
        bool isKnownLake(const MWWorld::CellStore* cell) const;
    };
}

#endif
