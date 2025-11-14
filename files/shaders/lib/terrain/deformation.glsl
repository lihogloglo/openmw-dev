#ifndef LIB_TERRAIN_DEFORMATION
#define LIB_TERRAIN_DEFORMATION

// Material-specific depth multipliers
float getDepthMultiplier(int materialType) {
    if (materialType == 1) // TERRAIN_SNOW
        return 1.0;
    else if (materialType == 2) // TERRAIN_SAND
        return 0.6;
    else if (materialType == 3) // TERRAIN_ASH
        return 0.3;
    return 0.0;
}

// Material-specific decay rates
float getDecayRate(int materialType) {
    if (materialType == 1) // TERRAIN_SNOW
        return 0.995;  // Slow decay
    else if (materialType == 2) // TERRAIN_SAND
        return 0.990;  // Medium decay
    else if (materialType == 3) // TERRAIN_ASH
        return 0.980;  // Fast decay
    return 0.0;
}

// Gaussian-like falloff for footprints
float getFootprintFalloff(float distance, float radius) {
    float normalized = distance / radius;
    return smoothstep(1.0, 0.0, normalized);
}

#endif
