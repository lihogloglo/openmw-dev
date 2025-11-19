#version 120

// Simplified Ocean Fragment Shader
// Water appearance without FFT foam textures

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/fragment.h.glsl"

// Ocean-specific parameters
varying vec3 vWorldPos;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vViewPos;

varying vec4 position;
varying float linearDepth;

// Parameters
uniform float uTime;
uniform float near;
uniform float far;
uniform vec2 screenRes;

uniform mat4 osg_ViewMatrixInverse;

#define PER_PIXEL_LIGHTING 0

#include "../shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "../fog.glsl"

// Water colors
const vec3 WATER_COLOR = vec3(0.0, 0.3, 0.4);
const vec3 DEEP_WATER_COLOR = vec3(0.0, 0.05, 0.15);
const vec3 SHALLOW_WATER_COLOR = vec3(0.0, 0.5, 0.6);

const float SPEC_HARDNESS = 256.0;
const float SPEC_BRIGHTNESS = 3.0;

// Schlick's Fresnel approximation
float fresnelSchlick(float cosTheta, float F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Procedural foam based on wave height and slope
float proceduralFoam(vec3 normal, vec3 worldPos, float time)
{
    // Foam appears on steep slopes (breaking waves)
    float steepness = 1.0 - normal.z;

    // Animated foam pattern using multiple noise-like patterns
    float foam = 0.0;

    // Large foam patches
    vec2 foamCoord1 = worldPos.xy * 0.05 + time * 0.02;
    foam += sin(foamCoord1.x * 3.14) * sin(foamCoord1.y * 3.14) * 0.5 + 0.5;

    // Small foam detail
    vec2 foamCoord2 = worldPos.xy * 0.15 + time * 0.05;
    foam += sin(foamCoord2.x * 6.28) * sin(foamCoord2.y * 6.28) * 0.3 + 0.3;

    // Apply steepness mask
    foam *= smoothstep(0.3, 0.7, steepness);

    return clamp(foam * 0.8, 0.0, 1.0);
}

void main(void)
{
    // View Space Normal
    vec3 normal = normalize(vNormal);

    // Compute shadow
    float shadow = unshadowedLightRatio(linearDepth);

    // View Space Light Direction (Directional Light 0)
    vec3 lightDir = normalize(gl_LightSource[0].position.xyz);

    // View Space View Direction (Fragment to Eye is -vViewPos)
    vec3 viewDir = normalize(-vViewPos.xyz);

    // World Space Light Direction (for Sky Color approximation)
    vec3 sunWorldDir = normalize((osg_ViewMatrixInverse * gl_LightSource[0].position).xyz);

    // Fresnel (water has F0 ~0.02)
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = fresnelSchlick(NdotV, 0.02);

    // Sky color for reflection (simple gradient based on sun)
    vec3 skyColor = mix(vec3(0.2, 0.3, 0.4), vec3(0.4, 0.6, 0.9), max(sunWorldDir.z, 0.0));

    // Add sunset/sunrise tint
    float sunsetFactor = smoothstep(-0.2, 0.1, sunWorldDir.z) * (1.0 - smoothstep(0.1, 0.3, sunWorldDir.z));
    skyColor = mix(skyColor, vec3(1.0, 0.5, 0.3), sunsetFactor * 0.3);

    // Water depth-based color (viewing angle dependent)
    vec3 waterColor = mix(SHALLOW_WATER_COLOR, DEEP_WATER_COLOR, 1.0 - NdotV * 0.5);

    // Ambient component
    vec3 ambient = waterColor * (gl_LightModel.ambient.rgb + vec3(0.3));

    // Diffuse lighting from sun
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuseColor = waterColor * gl_LightSource[0].diffuse.rgb * NdotL * 0.5 * shadow;

    // Specular highlights (Blinn-Phong)
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float specularTerm = pow(NdotH, SPEC_HARDNESS);
    vec3 specular = gl_LightSource[0].diffuse.rgb * specularTerm * SPEC_BRIGHTNESS * shadow;

    // Subsurface scattering approximation
    float subsurface = max(dot(normal, -lightDir), 0.0) * 0.4 * (1.0 - NdotV);
    vec3 subsurfaceColor = SHALLOW_WATER_COLOR * subsurface;

    // Combine water color components
    vec3 finalWaterColor = ambient + diffuseColor + subsurfaceColor;

    // Mix with sky reflection based on Fresnel
    vec3 finalColor = mix(finalWaterColor, skyColor, fresnel * 0.9);

    // Add specular highlights on top
    finalColor += specular;

    // Add procedural foam
    float foam = proceduralFoam(normal, vWorldPos, uTime);
    vec3 foamColor = vec3(1.0, 1.0, 1.0);
    finalColor = mix(finalColor, foamColor, foam * 0.7);

    // Apply transparency (more opaque at glancing angles)
    float alpha = mix(0.85, 1.0, fresnel);

    gl_FragData[0] = vec4(finalColor, alpha);

#if @radialFog
    // Calculate radial depth for fog
    float radialDepth = length(vViewPos.xyz);
    gl_FragData[0] = applyFogAtDist(gl_FragData[0], radialDepth, linearDepth, far);
#endif

#if !@disableNormals
    gl_FragData[1].rgb = normal * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
