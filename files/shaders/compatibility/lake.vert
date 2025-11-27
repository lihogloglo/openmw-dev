#version 120

// Lake vertex shader
// Uses cell center uniform to avoid floating-point precision issues with large world coordinates

varying vec4 position;      // View-space position (for lighting, SSR)
varying float linearDepth;  // Linear depth for effects
varying vec3 worldPos;      // World position for normal map sampling
varying vec2 rippleMapUV;   // Ripple effect UVs

// Cell center in world space (set per-cell from C++)
uniform vec3 cellCenter;

// Player position for ripple UVs
uniform vec3 playerPos;

// Ripple map parameters
const float rippleMapSize = 512.0;
const float rippleMapWorldScale = 2.0;

void main()
{
    // Standard MVP transformation
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    // View-space position for lighting calculations
    position = gl_ModelViewMatrix * gl_Vertex;
    linearDepth = -position.z;

    // World position: cell center + local vertex offset
    // This avoids precision loss from reconstructing large world coordinates
    // gl_Vertex is in local space, centered at origin for each cell
    worldPos = cellCenter + gl_Vertex.xyz;

    // Ripple UVs centered on player
    rippleMapUV = (worldPos.xy - playerPos.xy + (rippleMapSize * rippleMapWorldScale / 2.0))
                  / rippleMapSize / rippleMapWorldScale;
}
