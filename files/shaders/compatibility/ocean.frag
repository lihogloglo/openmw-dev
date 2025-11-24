#version 330 compatibility

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/fragment.h.glsl"

// Ocean water fragment shader - FFT-based water rendering
// Based on GodotOceanWaves

const float VISIBILITY = 2500.0;
const vec3 WATER_COLOR = vec3(0.015, 0.110, 0.455); // Deep ocean blue

varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 texCoord;

uniform sampler2DArray normalMap;
uniform sampler2DArray spectrumMap; // DEBUG
uniform int numCascades;
uniform vec4 mapScales[4];
uniform vec3 sunDir;
uniform vec3 sunColor;

uniform float osg_SimulationTime;
uniform float near;
uniform float far;
uniform vec2 screenRes;

// Debug visualization toggle
uniform int debugVisualizeCascades; // 0 = off, 1 = on
uniform int debugVisualizeLOD;      // 0 = off, 1 = on

// Camera position in world space (for cascade selection)
uniform vec3 cameraPosition;
uniform vec3 nodePosition;

#define PER_PIXEL_LIGHTING 0

#include "shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "fog.glsl"
#include "lib/water/fresnel.glsl"
#include "lib/view/depth.glsl"

void main(void)
{
    vec2 screenCoords = gl_FragCoord.xy / screenRes;
    float shadow = unshadowedLightRatio(linearDepth);

    // Sample gradients from FFT cascade using snapped world position
    // mapScales format: vec4(uvScale, uvScale, displacementScale, normalScale)
    // normalMap stores: vec4(gradient.x, gradient.y, dhx_dx, foam)
    vec3 gradient = vec3(0.0);
    float foam = 0.0;

    // worldPos is calculated in vertex shader as: vec3(worldPosXY, vertPos.z) + nodePosition
    // Since nodePosition is (0, 0, height), worldPos.xy already contains the snapped world XY
    // We just need to subtract the Z offset to get the actual 2D world position
    vec2 worldPosXY = worldPos.xy;

    float distToCamera = length(worldPosXY - cameraPosition.xy);

    for (int i = 0; i < numCascades && i < 4; ++i) {
        vec2 uv = worldPosXY * mapScales[i].x;
        vec4 normalSample = texture(normalMap, vec3(uv, float(i)));

        // Distance-based falloff for normals
        float falloff = 1.0;

        // Cascade 0 (50m) fades out after 1000m (36265 MW units)
        if (i == 0) falloff = clamp(1.0 - (distToCamera - 36265.0) / 36265.0, 0.0, 1.0);
        // Cascade 1 (100m) fades out after 3000m
        else if (i == 1) falloff = clamp(1.0 - (distToCamera - 145060.0) / 72530.0, 0.0, 1.0);

        // Apply per-cascade normal scale to gradient
        gradient.xy += normalSample.xy * mapScales[i].w * falloff;
        foam += normalSample.w * falloff;
    }

    // Reconstruct normal from gradient (as done in Godot water shader)
    vec3 normal = normalize(vec3(-gradient.x, 1.0, -gradient.y));

    // Debug: Visualize cascade coverage with color coding
    if (debugVisualizeCascades == 1) {
        // Cascade colors: Red, Green, Blue, Yellow
        vec3 cascadeColors[4] = vec3[4](
            vec3(1.0, 0.0, 0.0),  // Cascade 0: Red (finest detail, 50m tiles)
            vec3(0.0, 1.0, 0.0),  // Cascade 1: Green (100m tiles)
            vec3(0.0, 0.5, 1.0),  // Cascade 2: Blue (200m tiles)
            vec3(1.0, 1.0, 0.0)   // Cascade 3: Yellow (broadest, 400m tiles)
        );

        // Use camera position uniform (already in world space)
        float distToCamera = length(worldPos.xy - cameraPosition.xy);

        // Select cascade based on distance from camera
        // Thresholds based on cascade tile sizes (in MW units):
        // Cascade 0 (red):    0-7,253 units (50-100m tiles)
        // Cascade 1 (green):  7,253-14,506 units (100-200m tiles)
        // Cascade 2 (blue):   14,506-29,012 units (200-400m tiles)
        // Cascade 3 (yellow): 29,012+ units (400m+ tiles)
        int selectedCascade = 3;
        if (distToCamera < 7253.0) selectedCascade = 0;
        else if (distToCamera < 14506.0) selectedCascade = 1;
        else if (distToCamera < 29012.0) selectedCascade = 2;

        vec3 debugColor = cascadeColors[selectedCascade];

        // Draw grid for selected cascade only
        vec2 uv = worldPos.xy * mapScales[selectedCascade].x;
        vec2 uvFrac = fract(uv);

        // Grid lines for this cascade
        float gridLineX = step(0.99, uvFrac.x) + step(uvFrac.x, 0.01);
        float gridLineY = step(0.99, uvFrac.y) + step(uvFrac.y, 0.01);
        float isOnGrid = max(gridLineX, gridLineY);

        if (isOnGrid > 0.5) {
            debugColor = vec3(0.0); // Black grid lines
        }

        gl_FragData[0] = vec4(debugColor, 0.9);
        applyShadowDebugOverlay();
        return;
    }

    // Debug: Visualize LOD rings with grid density
    if (debugVisualizeLOD == 1) {
        float dist = length(worldPos.xy - cameraPosition.xy);
        float gridSize = 0.0;
        vec3 ringColor = vec3(1.0);

        // Match ring radii and grid sizes from ocean.cpp
        if (dist < 1000.0) {
            gridSize = 3.90625; // Ring 0
            ringColor = vec3(1.0, 0.0, 0.0); // Red
        } else if (dist < 1813.0) {
            gridSize = 14.164; // Ring 1
            ringColor = vec3(1.0, 0.5, 0.0); // Orange
        } else if (dist < 3626.0) {
            gridSize = 56.664; // Ring 2
            ringColor = vec3(1.0, 1.0, 0.0); // Yellow
        } else if (dist < 7253.0) {
            gridSize = 226.656; // Ring 3
            ringColor = vec3(0.0, 1.0, 0.0); // Green
        } else if (dist < 14506.0) {
            gridSize = 906.625; // Ring 4
            ringColor = vec3(0.0, 1.0, 1.0); // Cyan
        } else if (dist < 29012.0) {
            gridSize = 1813.25; // Ring 5
            ringColor = vec3(0.0, 0.0, 1.0); // Blue
        } else if (dist < 58024.0) {
            gridSize = 3626.5; // Ring 6
            ringColor = vec3(0.5, 0.0, 1.0); // Purple
        } else if (dist < 116048.0) {
            gridSize = 7253.0; // Ring 7
            ringColor = vec3(1.0, 0.0, 1.0); // Magenta
        } else if (dist < 232096.0) {
            gridSize = 14506.0; // Ring 8
            ringColor = vec3(0.5, 0.5, 0.5); // Grey
        } else {
            gridSize = 58024.0; // Ring 9
            ringColor = vec3(0.2, 0.2, 0.2); // Dark Grey
        }

        // Draw grid lines
        vec2 gridUV = worldPos.xy / gridSize;
        vec2 gridFrac = fract(gridUV);
        float lineThickness = 0.02; // Adjust for visibility
        float gridX = step(1.0 - lineThickness, gridFrac.x) + step(gridFrac.x, lineThickness);
        float gridY = step(1.0 - lineThickness, gridFrac.y) + step(gridFrac.y, lineThickness);
        float isGrid = max(gridX, gridY);

        vec3 finalDebugColor = mix(ringColor * 0.2, vec3(1.0), isGrid); // Faint background, white grid
        gl_FragData[0] = vec4(finalDebugColor, 1.0);
        applyShadowDebugOverlay();
        return;
    }

    // Clamp foam to reasonable range
    foam = clamp(foam * 0.75, 0.0, 1.0); // Scale down since we accumulate from multiple cascades

    // Mix water color with foam color
    const vec3 FOAM_COLOR = vec3(0.95, 0.95, 0.95); // White foam
    vec3 baseColor = mix(WATER_COLOR, FOAM_COLOR, smoothstep(0.0, 1.0, foam));

    // Simple lighting with normal
    float upFacing = max(0.0, normal.z);
    vec3 finalColor = baseColor * (0.3 + upFacing * 0.7);

    // Reduce alpha where there's foam (foam is more opaque)
    float alpha = mix(0.85, 0.95, foam);

    gl_FragData[0] = vec4(finalColor, alpha);
    
    // DEBUG: Uncomment one of these lines to visualize different components
    // gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0); // Solid Red (Geometry Check)
    // gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0); // Normals
    // gl_FragData[0] = vec4(WATER_COLOR, 1.0); // Solid water color test

    // DEBUG: Visualize distance to camera (gray gradient, darker = closer)
    // float distToCamera = length(worldPos.xy - cameraPosition.xy);
    // gl_FragData[0] = vec4(vec3(distToCamera / 50000.0), 1.0); return;

    // DEBUG: Visualize worldPos (should change as you move)
    // gl_FragData[0] = vec4(fract(worldPos.xyz * 0.0001), 1.0); return;

    // DEBUG: Visualize displacement magnitude from all cascades
    // vec3 totalDisp = vec3(0.0);
    // for (int i = 0; i < numCascades && i < 4; ++i) {
    //     vec2 uv = worldPos.xy * mapScales[i].x;
    //     vec3 disp = texture(displacementMap, vec3(uv, float(i))).xyz;
    //     totalDisp += disp * mapScales[i].z;
    // }
    // float dispMag = length(totalDisp);
    // gl_FragData[0] = vec4(vec3(dispMag * 0.01), 1.0); return; // Scale for visibility
    
    // Full rendering (currently disabled for testing)
    // gl_FragData[0] = colorWithFog;
    
    // DEBUG: Visualize Spectrum (Cascade 0)
    // vec4 spectrum = texture(spectrumMap, vec3(worldPos.xy * mapScales[0].x, 0.0));
    // gl_FragData[0] = vec4(spectrum.xyz, 1.0); // Show raw S, D, Amp
    
    // gl_FragData[0] = vec4(vec3(shadow), 1.0); // Shadow
    // gl_FragData[0] = vec4(vec3(linearDepth / 5000.0), 1.0); // Depth

    applyShadowDebugOverlay();
}
