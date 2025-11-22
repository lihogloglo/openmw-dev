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


    // Progressive test: Add back lighting, no fog yet
    vec3 cameraPos = (gl_ModelViewMatrixInverse * vec4(0,0,0,1)).xyz;
    float upFacing = max(0.0, normal.z);
    vec3 testColor = WATER_COLOR * (0.3 + upFacing * 0.7);
    gl_FragData[0] = vec4(testColor, 0.85);
    
    // DEBUG: Uncomment one of these lines to visualize different components
    // gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0); // Solid Red (Geometry Check)
    // gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0); // Normals
    // gl_FragData[0] = vec4(WATER_COLOR, 1.0); // Solid water color test
    
    // Full rendering (currently disabled for testing)
    // gl_FragData[0] = colorWithFog;
    
    // DEBUG: Visualize Spectrum (Cascade 0)
    // vec4 spectrum = texture(spectrumMap, vec3(worldPos.xy * mapScales[0].x, 0.0));
    // gl_FragData[0] = vec4(spectrum.xyz, 1.0); // Show raw S, D, Amp
    
    // gl_FragData[0] = vec4(vec3(shadow), 1.0); // Shadow
    // gl_FragData[0] = vec4(vec3(linearDepth / 5000.0), 1.0); // Depth

    applyShadowDebugOverlay();
}
