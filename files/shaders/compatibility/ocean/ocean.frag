#version 120

// Ocean Fragment Shader
// Renders FFT-based ocean waves with simplified material (no RTT for now)

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/fragment.h.glsl"

// Ocean-specific parameters
varying vec3 vWorldPos;
varying vec3 vDisplacedPos;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vViewPos;

varying vec4 position;
varying float linearDepth;

// FFT textures
uniform sampler2D uFoamCascade0;
uniform sampler2D uFoamCascade1;
uniform float uCascadeTileSize0;
uniform float uCascadeTileSize1;

// Simplified water appearance
uniform float near;
uniform float far;
uniform vec2 screenRes;

const vec3 WATER_COLOR = vec3(0.090195, 0.115685, 0.12745);
const vec3 DEEP_WATER_COLOR = vec3(0.0, 0.05, 0.1);
const vec3 SHALLOW_WATER_COLOR = vec3(0.0, 0.3, 0.4);

const float SPEC_HARDNESS = 128.0;
const float SPEC_BRIGHTNESS = 2.0;

#define PER_PIXEL_LIGHTING 0

#include "../shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "../fog.glsl"

// Schlick's Fresnel approximation
float fresnelSchlick(float cosTheta, float F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Sample foam from FFT cascades
float sampleFoam(vec2 worldPosXY)
{
    vec2 uv0 = worldPosXY / uCascadeTileSize0;
    vec2 uv1 = worldPosXY / uCascadeTileSize1;

    float foam0 = texture2D(uFoamCascade0, uv0).r;
    float foam1 = texture2D(uFoamCascade1, uv1).r;

    // Blend cascades
    return max(foam0, foam1 * 0.5);
}

void main(void)
{
    // Use the FFT-generated normal (already interpolated from vertex shader)
    vec3 normal = normalize(vNormal);

    float shadow = unshadowedLightRatio(linearDepth);

    vec3 sunWorldDir = normalize((gl_ModelViewMatrixInverse * vec4(lcalcPosition(0).xyz, 0.0)).xyz);
    vec3 cameraPos = (gl_ModelViewMatrixInverse * vec4(0,0,0,1)).xyz;
    vec3 viewDir = normalize(position.xyz - cameraPos.xyz);

    float sunFade = length(gl_LightModel.ambient.xyz);

    // Fresnel (water has F0 ~0.02)
    float fresnel = fresnelSchlick(max(dot(normal, -viewDir), 0.0), 0.02);

    // Approximate sky color for reflection
    vec3 skyColor = mix(vec3(0.3, 0.4, 0.5), vec3(0.5, 0.7, 1.0), max(sunWorldDir.z, 0.0));

    // Water color based on viewing angle (deeper when looking down)
    float viewAngle = abs(dot(normal, -viewDir));
    float depthFactor = 1.0 - viewAngle;
    vec3 waterColor = mix(SHALLOW_WATER_COLOR, DEEP_WATER_COLOR, depthFactor * 0.8) * sunFade;

    // Ambient component
    vec3 ambient = waterColor * 0.4;

    // Diffuse lighting from sun
    float diffuse = max(dot(normal, sunWorldDir), 0.0);
    vec3 diffuseColor = waterColor * diffuse * 0.6;

    // Specular highlights (Blinn-Phong)
    vec3 halfVec = normalize(sunWorldDir - viewDir);
    float specularTerm = pow(max(dot(normal, halfVec), 0.0), SPEC_HARDNESS);
    vec3 specular = vec3(1.0) * specularTerm * SPEC_BRIGHTNESS * shadow;

    // Subsurface scattering approximation
    float subsurface = max(dot(normal, sunWorldDir), 0.0) * 0.3;
    vec3 subsurfaceColor = SHALLOW_WATER_COLOR * subsurface;

    // Combine water color components
    vec3 finalWaterColor = ambient + diffuseColor + subsurfaceColor;

    // Mix with sky reflection based on Fresnel
    vec3 finalColor = mix(finalWaterColor, skyColor, fresnel * 0.7);

    // Add specular highlights on top
    finalColor += specular;

    // Add foam from FFT simulation
    float foam = sampleFoam(vWorldPos.xy);
    vec3 foamColor = vec3(1.0, 1.0, 1.0);

    // Make foam visible and blend naturally
    float foamStrength = smoothstep(0.4, 0.8, foam);
    finalColor = mix(finalColor, foamColor, foamStrength * 0.9);

    // Apply transparency
    float alpha = mix(0.5, 0.95, fresnel);

    gl_FragData[0] = vec4(finalColor, alpha);

#if @radialFog
    float radialDepth = distance(position.xyz, cameraPos);
    gl_FragData[0] = applyFogAtDist(gl_FragData[0], radialDepth, linearDepth, far);
#endif

#if !@disableNormals
    gl_FragData[1].rgb = normalize(gl_NormalMatrix * normal) * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
