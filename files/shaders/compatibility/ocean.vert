#version 330 compatibility

// Don't include lib/core/vertex.h.glsl due to version conflicts
// Manually define the required functions
uniform mat4 projectionMatrix;

vec4 modelToView(vec4 pos)
{
    return gl_ModelViewMatrix * pos;
}

vec4 modelToClip(vec4 pos)
{
    return projectionMatrix * modelToView(pos);
}

varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 texCoord;
varying float waveHeight; // Y component of displacement for subsurface scattering

#include "shadows_vertex.glsl"
#include "lib/view/depth.glsl"

uniform vec3 nodePosition;
uniform sampler2DArray displacementMap;
uniform int numCascades;
uniform vec3 cameraPosition; // Added for distance calculation
uniform vec4 mapScales[4]; // xy = scale for each cascade

// Shore smoothing - manual control for vertex displacement
// This is a global multiplier (0-1) that reduces large wave displacement
// Set via Lua: ocean.setVertexShoreSmoothing(0.5) to reduce big waves by 50%
uniform float vertexShoreSmoothing; // 0 = full waves, 1 = no displacement (calm)

// Shore distance map - automatic shore detection based on terrain
// Format: R16F texture where 0 = on shore, 1 = far from shore (open ocean)
uniform sampler2D shoreDistanceMap;
uniform vec4 shoreMapBounds;     // vec4(minX, minY, maxX, maxY) in world coords
uniform int hasShoreDistanceMap; // 0 = disabled, 1 = enabled

void main(void)
{
    position = gl_Vertex;
    texCoord = gl_MultiTexCoord0.xy;

    // Clipmap approach: mesh is stationary, vertices offset to follow camera
    // This prevents texture swimming when the mesh moves
    vec3 vertPos = position.xyz;

    // Calculate world position by offsetting vertex relative to camera
    // Mesh stays at origin, but we offset vertices to center on camera
    const float CASCADE_0_RADIUS = 50.0 * 72.53 / 2.0;  // 1813.25 units
    const float RING_0_GRID_SIZE = 512.0;
    float gridSnapSize = (2.0 * CASCADE_0_RADIUS) / RING_0_GRID_SIZE; // ~7.082 units

    // Snap camera position to grid for mesh positioning AND displacement sampling
    // This ensures vertices land on consistent UV coordinates and prevents texture swimming
    vec2 snappedCameraPos = floor(cameraPosition.xy / gridSnapSize) * gridSnapSize;

    // Calculate world position for UV sampling using SNAPPED camera position
    // This ensures displacement and normals are sampled from consistent locations
    vec2 worldPosXY = vertPos.xy + snappedCameraPos;

    vec3 totalDisplacement = vec3(0.0);
    float dist = length(vertPos.xy);

    // ========================================================================
    // SHORE DISTANCE SAMPLING
    // ========================================================================
    // Calculate shore smoothing factor from either:
    // 1. Shore distance map (automatic, if available)
    // 2. Manual vertexShoreSmoothing uniform (fallback)
    float shoreSmoothing = vertexShoreSmoothing;

    if (hasShoreDistanceMap == 1)
    {
        // Convert world position to UV in shore map
        vec2 shoreUV = (worldPosXY - shoreMapBounds.xy) / (shoreMapBounds.zw - shoreMapBounds.xy);

        // Clamp to valid range (outside map = assume open ocean)
        if (shoreUV.x >= 0.0 && shoreUV.x <= 1.0 && shoreUV.y >= 0.0 && shoreUV.y <= 1.0)
        {
            // Sample shore distance (0 = shore, 1 = open ocean)
            float shoreDistance = texture(shoreDistanceMap, shoreUV).r;

            // Invert: we want 0 = open ocean (full waves), 1 = shore (calm)
            shoreSmoothing = 1.0 - shoreDistance;
        }
        else
        {
            // Outside map bounds - assume open ocean (no smoothing)
            shoreSmoothing = 0.0;
        }
    }

    // Per-cascade shore weights for displacement
    // Large waves (cascade 0-1) are suppressed more, small ripples (2-3) less
    // shoreSmoothing = 0: full waves, shoreSmoothing = 1: calm water
    float cascadeDispWeights[4];
    cascadeDispWeights[0] = 1.0 - shoreSmoothing;                    // Large swells: full suppression
    cascadeDispWeights[1] = 1.0 - shoreSmoothing * 0.7;              // Medium waves: 70% suppression
    cascadeDispWeights[2] = 1.0 - shoreSmoothing * 0.3;              // Small ripples: 30% suppression
    cascadeDispWeights[3] = 1.0 - shoreSmoothing * 0.1;              // Fine detail: 10% suppression

    // Displacement sampling with per-cascade amplitude scaling and shore attenuation
    for (int i = 0; i < numCascades && i < 4; ++i) {
        vec2 uv = worldPosXY * mapScales[i].x;
        vec3 disp = texture(displacementMap, vec3(uv, float(i))).xyz;

        totalDisplacement += disp * mapScales[i].z * cascadeDispWeights[i];
    }

    // Distance-based displacement falloff (matching Godot water.gdshader line 29)
    // Godot: float distance_factor = min(exp(-(length(VERTEX.xz - CAMERA_POSITION_WORLD.xz) - 150.0)*0.007), 1.0);
    // Falloff starts at 300 meters (doubled from 150m) and gradually reduces displacement
    // Halved the falloff rate (0.007 -> 0.0035) to make falloff happen 2x farther away
    const float DISPLACEMENT_FALLOFF_START = 300.0 * 72.53; // 21,759 MW units
    const float DISPLACEMENT_FALLOFF_RATE = 0.0035;
    float distanceFromCamera = length(vertPos.xy); // Distance in local space
    float displacementFalloff = min(exp(-(distanceFromCamera - DISPLACEMENT_FALLOFF_START) * DISPLACEMENT_FALLOFF_RATE), 1.0);
    totalDisplacement *= displacementFalloff;

    // Apply displacement AND camera offset to local vertex position
    // We need to offset the mesh vertices to follow the camera
    vec3 displacedLocalPos = vertPos + vec3(snappedCameraPos, 0.0) + totalDisplacement;

    // World position for fragment shader (used for normal sampling)
    // MUST use snappedCameraPos to match the actual vertex position
    worldPos = vec3(vertPos.xy + snappedCameraPos, vertPos.z) + totalDisplacement + nodePosition;

    // Pass wave height to fragment shader for subsurface scattering
    waveHeight = totalDisplacement.y;

    // Transform to clip space using the offset local coordinates
    gl_Position = modelToClip(vec4(displacedLocalPos, 1.0));

    vec4 viewPos = modelToView(vec4(displacedLocalPos, 1.0));
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    setupShadowCoords(viewPos, normalize((gl_NormalMatrix * gl_Normal).xyz));
}
