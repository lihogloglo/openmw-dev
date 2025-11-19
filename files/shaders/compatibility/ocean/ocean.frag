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

uniform mat4 osg_ViewMatrixInverse;

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
    // View Space Normal
    vec3 normal = normalize(vNormal);

    float shadow = unshadowedLightRatio(linearDepth);

    // View Space Light Direction (Directional Light 0)
    vec3 lightDir = normalize(gl_LightSource[0].position.xyz);

    // View Space View Direction (Fragment to Eye is -vViewPos)
    vec3 viewDir = normalize(-vViewPos.xyz);

    // World Space Light Direction (for Sky Color approximation)
    // gl_LightSource[0].position is View Space. Transform to World.
    // Note: Directional light position.w == 0, so translation doesn't apply.
    vec3 sunWorldDir = normalize((osg_ViewMatrixInverse * gl_LightSource[0].position).xyz);

    float sunFade = length(gl_LightModel.ambient.xyz); // Hacky way to detect night/day brightness?

    // Fresnel (water has F0 ~0.02)
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = fresnelSchlick(NdotV, 0.02);

    // Approximate sky color for reflection
    // Simple gradient based on sun height
    vec3 skyColor = mix(vec3(0.3, 0.4, 0.5), vec3(0.5, 0.7, 1.0), max(sunWorldDir.z, 0.0));

    // Water color based on viewing angle (deeper when looking down)
    // When NdotV is 1 (looking straight down), depthFactor is 0 -> Deep Color?
    // Usually: Looking down -> See bottom (transparency). Deep water -> Dark.
    // Glancing angle -> Reflective.
    // Let's mix based on NdotV.
    vec3 waterColor = mix(SHALLOW_WATER_COLOR, DEEP_WATER_COLOR, 0.5); // Base color

    // Ambient component
    vec3 ambient = waterColor * 0.4;

    // Diffuse lighting from sun
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuseColor = waterColor * NdotL * 0.6;

    // Specular highlights (Blinn-Phong)
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float specularTerm = pow(NdotH, SPEC_HARDNESS);
    vec3 specular = vec3(1.0) * specularTerm * SPEC_BRIGHTNESS * shadow;

    // Subsurface scattering approximation (light passing through wave)
    // Fake SSS: Light visible when looking against light through wave?
    // Simple wrap lighting or similar.
    float subsurface = max(dot(normal, -lightDir), 0.0) * 0.3 * (1.0 - NdotV); 
    vec3 subsurfaceColor = SHALLOW_WATER_COLOR * subsurface;

    // Combine water color components
    vec3 finalWaterColor = ambient + diffuseColor + subsurfaceColor;

    // Mix with sky reflection based on Fresnel
    vec3 finalColor = mix(finalWaterColor, skyColor, fresnel * 0.8);

    // Add specular highlights on top
    finalColor += specular;

    // Add foam from FFT simulation
    float foam = sampleFoam(vWorldPos.xy);
    vec3 foamColor = vec3(1.0, 1.0, 1.0);

    // Make foam visible and blend naturally
    // Foam accumulates at wave peaks/choppy areas
    float foamStrength = smoothstep(0.4, 0.8, foam);
    finalColor = mix(finalColor, foamColor, foamStrength * 0.9);

    // Apply transparency
    // More transparent at perpendicular angles (looking down), more opaque at glancing angles (Fresnel)
    float alpha = mix(0.8, 1.0, fresnel);

    gl_FragData[0] = vec4(finalColor, alpha);

#if @radialFog
    // Calculate radial depth for fog
    // vViewPos is negative Z forward. Distance is length.
    float radialDepth = length(vViewPos.xyz);
    gl_FragData[0] = applyFogAtDist(gl_FragData[0], radialDepth, linearDepth, far);
#endif

#if !@disableNormals
    gl_FragData[1].rgb = normal * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
