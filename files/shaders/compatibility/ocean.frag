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

    // Basic lighting
    vec3 cameraPos = (gl_ModelViewMatrixInverse * vec4(0,0,0,1)).xyz;
    vec3 viewDir = normalize(position.xyz - cameraPos.xyz);
    vec3 sunWorldDir = normalize((gl_ModelViewMatrixInverse * vec4(lcalcPosition(0).xyz, 0.0)).xyz);

    // Fresnel effect
    float ior = (cameraPos.z > 0.0) ? (1.333/1.0) : (1.0/1.333);
    float fresnel = clamp(fresnel_dielectric(viewDir, normal, ior), 0.0, 1.0);

    // Simple diffuse lighting
    float diffuse = max(dot(normal, sunWorldDir), 0.0);

    // Specular
    vec3 halfVec = normalize(sunWorldDir - viewDir);
    float specular = pow(max(dot(normal, halfVec), 0.0), 128.0);

    // Combine
    vec3 waterCol = WATER_COLOR;
    vec3 color = waterCol * (0.3 + diffuse * 0.7) * shadow;
    color += sunColor * specular * 1.5 * shadow;

    // Apply fresnel for reflections (simplified - no actual reflection sampling)
    color = mix(color, vec3(0.5, 0.7, 0.9), fresnel * 0.5); // Sky color approximation

    // Fog - use already-declared cameraPos from line 61
    float euclideanDepth = length(position.xyz - cameraPos);
    vec4 colorWithFog = applyFogAtDist(vec4(color, 0.85), euclideanDepth, linearDepth, far);

    // DEBUG: Uncomment one of these lines to visualize different components
    // gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0); // Solid Red (Geometry Check)
    gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0); // Normals
    
    // DEBUG: Visualize Spectrum (Cascade 0)
    vec4 spectrum = texture(spectrumMap, vec3(worldPos.xy * mapScales[0].x, 0.0));
    gl_FragData[0] = vec4(abs(spectrum.xy), 0.0, 1.0); // Raw values (noise should be visible ~1.0)
    
    // gl_FragData[0] = vec4(vec3(shadow), 1.0); // Shadow
    // gl_FragData[0] = vec4(vec3(linearDepth / 5000.0), 1.0); // Depth

    applyShadowDebugOverlay();
}
