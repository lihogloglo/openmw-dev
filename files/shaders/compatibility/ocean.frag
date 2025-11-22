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

    // Fog
    color = applyFog(color, linearDepth);

    gl_FragData[0].xyz = color;
    gl_FragData[0].w = 0.85;

    applyShadowDebugOverlay();
}
