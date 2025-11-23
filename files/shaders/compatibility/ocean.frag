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

// Camera position in world space (for cascade selection)
uniform vec3 cameraPosition;

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

    // Sample normal from FFT cascade (simplified - normally would blend multiple cascades)
    vec3 normal = vec3(0.0, 0.0, 1.0);

    // Normal sampling from cascade
    for (int i = 0; i < numCascades && i < 4; ++i) {
        vec2 uv = worldPos.xy * mapScales[i].x;
        vec4 normalSample = texture(normalMap, vec3(uv, float(i)));
        normal.xy += normalSample.xy; // gradient
    }

    normal = normalize(normal);

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

    // Progressive test: Add back lighting, no fog yet
    float upFacing = max(0.0, normal.z);
    vec3 testColor = WATER_COLOR * (0.3 + upFacing * 0.7);
    gl_FragData[0] = vec4(testColor, 0.85);
    
    // DEBUG: Uncomment one of these lines to visualize different components
    // gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0); // Solid Red (Geometry Check)
    // gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0); // Normals
    // gl_FragData[0] = vec4(WATER_COLOR, 1.0); // Solid water color test

    // DEBUG: Visualize distance to camera (gray gradient, darker = closer)
    // float distToCamera = length(worldPos.xy - cameraPosition.xy);
    // gl_FragData[0] = vec4(vec3(distToCamera / 50000.0), 1.0); return;

    // DEBUG: Visualize worldPos (should change as you move)
    // gl_FragData[0] = vec4(fract(worldPos.xyz * 0.0001), 1.0); return;
    
    // Full rendering (currently disabled for testing)
    // gl_FragData[0] = colorWithFog;
    
    // DEBUG: Visualize Spectrum (Cascade 0)
    // vec4 spectrum = texture(spectrumMap, vec3(worldPos.xy * mapScales[0].x, 0.0));
    // gl_FragData[0] = vec4(spectrum.xyz, 1.0); // Show raw S, D, Amp
    
    // gl_FragData[0] = vec4(vec3(shadow), 1.0); // Shadow
    // gl_FragData[0] = vec4(vec3(linearDepth / 5000.0), 1.0); // Depth

    applyShadowDebugOverlay();
}
