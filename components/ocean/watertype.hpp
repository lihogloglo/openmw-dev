#ifndef OPENMW_COMPONENTS_OCEAN_WATERTYPE_H
#define OPENMW_COMPONENTS_OCEAN_WATERTYPE_H

#include <string_view>

namespace Ocean
{
    /// Classification of water bodies for different rendering approaches
    enum class WaterType
    {
        OCEAN,      // Large exterior water, full wave simulation
        LARGE_LAKE, // Large bodies (>100 cells²), reduced waves
        SMALL_LAKE, // Medium ponds (10-100 cells²), minimal waves
        POND,       // Tiny water (<10 cells²), no waves
        RIVER,      // Flowing water, current-aligned waves (future)
        INDOOR      // Interior cells, completely flat
    };

    /// Get string representation of water type (for debugging/logging)
    constexpr std::string_view waterTypeToString(WaterType type)
    {
        switch (type)
        {
            case WaterType::OCEAN:
                return "Ocean";
            case WaterType::LARGE_LAKE:
                return "Large Lake";
            case WaterType::SMALL_LAKE:
                return "Small Lake";
            case WaterType::POND:
                return "Pond";
            case WaterType::RIVER:
                return "River";
            case WaterType::INDOOR:
                return "Indoor";
        }
        return "Unknown";
    }

    /// Parameters for ocean wave simulation
    struct OceanParams
    {
        float windSpeed{ 10.0f };      // m/s
        float windDirectionX{ 1.0f };  // Wind direction (normalized)
        float windDirectionY{ 0.0f };
        float fetchDistance{ 100000.0f }; // meters (effectively infinite for Morrowind)
        float waterDepth{ 1000.0f };   // meters (deep water assumption)
        float choppiness{ 1.5f };      // Wave steepness multiplier
    };
}

#endif
