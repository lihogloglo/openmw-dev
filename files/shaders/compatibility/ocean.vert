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

    // Snap camera position to grid for mesh positioning
    vec2 snappedCameraPos = floor(cameraPosition.xy / gridSnapSize) * gridSnapSize;

    // Calculate world position for UV sampling using UNSNAPPED camera position
    // This ensures displacement is continuous as camera moves
    vec2 worldPosXY = vertPos.xy + cameraPosition.xy;

    vec3 totalDisplacement = vec3(0.0);
    float dist = length(vertPos.xy);

    // Displacement sampling with per-cascade amplitude scaling
    for (int i = 0; i < numCascades && i < 4; ++i) {
        vec2 uv = worldPosXY * mapScales[i].x;
        vec3 disp = texture(displacementMap, vec3(uv, float(i))).xyz;

        // Distance-based falloff for displacement
        float falloff = 1.0;

        // Cascade 0 (50m) fades out after 1000m (in MW units: 72530)
        if (i == 0) falloff = clamp(1.0 - (dist - 36265.0) / 36265.0, 0.0, 1.0);
        // Cascade 1 (100m) fades out after 3000m (in MW units: 217590)
        else if (i == 1) falloff = clamp(1.0 - (dist - 145060.0) / 72530.0, 0.0, 1.0);

        totalDisplacement += disp * mapScales[i].z * falloff;
    }

    // Apply displacement AND camera offset to local vertex position
    // We need to offset the mesh vertices to follow the camera
    vec3 displacedLocalPos = vertPos + vec3(snappedCameraPos, 0.0) + totalDisplacement;

    // World position for fragment shader (used for normal sampling)
    // This is the actual world-space position after offsetting and displacement
    worldPos = vec3(worldPosXY, vertPos.z) + totalDisplacement + nodePosition;

    // Pass wave height to fragment shader for subsurface scattering
    waveHeight = totalDisplacement.y;

    // Transform to clip space using the offset local coordinates
    gl_Position = modelToClip(vec4(displacedLocalPos, 1.0));

    vec4 viewPos = modelToView(vec4(displacedLocalPos, 1.0));
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    setupShadowCoords(viewPos, normalize((gl_NormalMatrix * gl_Normal).xyz));
}
