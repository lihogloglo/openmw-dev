#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION FRAGMENT SHADER (Compatibility Profile)
// ============================================================================
// Handles lighting, texturing, and visual effects for tessellated terrain.
// Receives interpolated data from the tessellation evaluation shader.
// Uses compatibility profile to access gl_NormalMatrix, gl_FrontMaterial, etc.
// Integrates with OpenMW's lighting system for correct sun/light interaction.
// ============================================================================

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

// Inputs from tessellation evaluation shader
in TES_OUT {
    vec2 texCoord;
    vec3 normal;
    vec4 color;
    vec3 viewPos;
    float euclideanDepth;
    float linearDepth;
} fs_in;

// Outputs
out vec4 fragColor;
#if @writeNormals
out vec4 fragNormal;
#endif

// Texture samplers
uniform sampler2D diffuseMap;
#if @normalMap
uniform sampler2D normalMap;
#endif
#if @blendMap
uniform sampler2D blendMap;
#endif

// Environment uniforms
uniform float far;
uniform vec2 screenRes;

// Texture matrices (for tiling)
uniform mat4 textureMatrix0;
uniform mat4 textureMatrix1;

// Define PER_PIXEL_LIGHTING for lighting.glsl
#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

// Color mode uniform
uniform int colorMode;
const int ColorMode_None = 0;
const int ColorMode_Emission = 1;
const int ColorMode_AmbientAndDiffuse = 2;
const int ColorMode_Ambient = 3;
const int ColorMode_Diffuse = 4;
const int ColorMode_Specular = 5;

// Color functions
vec4 getEmissionColor()
{
    if (colorMode == ColorMode_Emission)
        return fs_in.color;
    return gl_FrontMaterial.emission;
}

vec4 getAmbientColor()
{
    if (colorMode == ColorMode_AmbientAndDiffuse || colorMode == ColorMode_Ambient)
        return fs_in.color;
    return gl_FrontMaterial.ambient;
}

vec4 getDiffuseColor()
{
    if (colorMode == ColorMode_AmbientAndDiffuse || colorMode == ColorMode_Diffuse)
        return fs_in.color;
    return gl_FrontMaterial.diffuse;
}

vec4 getSpecularColor()
{
    if (colorMode == ColorMode_Specular)
        return fs_in.color;
    return gl_FrontMaterial.specular;
}

// Include OpenMW's lighting and shadow systems
#include "compatibility/shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "compatibility/fog.glsl"

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    // Calculate adjusted UV with texture matrix (for tiling)
    vec2 adjustedUV = (textureMatrix0 * vec4(fs_in.texCoord, 0.0, 1.0)).xy;

    // Sample diffuse texture
    vec4 diffuseTex = texture(diffuseMap, adjustedUV);
    fragColor = vec4(diffuseTex.rgb, 1.0);

    fragColor.a *= getDiffuseColor().a;

    // ========================================================================
    // BLEND MAP
    // ========================================================================

#if @blendMap
    vec2 blendMapUV = (textureMatrix1 * vec4(fs_in.texCoord, 0.0, 1.0)).xy;
    fragColor.a *= texture(blendMap, blendMapUV).a;
#endif

    // ========================================================================
    // NORMAL CALCULATION
    // ========================================================================

    vec3 viewNormal;

#if @normalMap
    vec4 normalTex = texture(normalMap, adjustedUV);
    vec3 tangentNormal = normalTex.xyz * 2.0 - 1.0;
    #if @reconstructNormalZ
    tangentNormal.z = sqrt(1.0 - dot(tangentNormal.xy, tangentNormal.xy));
    #endif
    // For terrain, assume mostly flat and use simplified transform
    viewNormal = normalize(gl_NormalMatrix * vec3(tangentNormal.xy, tangentNormal.z));
#else
    viewNormal = normalize(gl_NormalMatrix * fs_in.normal);
#endif

    // ========================================================================
    // LIGHTING - Using OpenMW's lighting system
    // ========================================================================

    float shadowing = unshadowedLightRatio(fs_in.linearDepth);
    vec3 lighting, specular;

#if PER_PIXEL_LIGHTING
    #if @specularMap
        float materialShininess = 128.0;
        vec3 specularColor = vec3(diffuseTex.a);
    #else
        float materialShininess = gl_FrontMaterial.shininess;
        vec3 specularColor = getSpecularColor().xyz;
    #endif
    vec3 diffuseLight, ambientLight, specularLight;
    doLighting(fs_in.viewPos, viewNormal, materialShininess, shadowing, diffuseLight, ambientLight, specularLight);
    lighting = getDiffuseColor().xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    specular = specularColor * specularLight;
#else
    // Vertex lighting fallback
    vec3 diffuseLight, ambientLight, specularLight;
    vec3 shadowDiffuse, shadowSpecular;
    doLighting(fs_in.viewPos, viewNormal, gl_FrontMaterial.shininess, diffuseLight, ambientLight, specularLight, shadowDiffuse, shadowSpecular);
    lighting = getDiffuseColor().xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    lighting += shadowDiffuse * shadowing;
    specular = getSpecularColor().xyz * specularLight + shadowSpecular * shadowing;
#endif

    clampLightingResult(lighting);
    fragColor.rgb = fragColor.rgb * lighting + specular;

    // ========================================================================
    // FOG - Using OpenMW's fog system
    // ========================================================================

    fragColor = applyFogAtDist(fragColor, fs_in.euclideanDepth, fs_in.linearDepth, far);

    // ========================================================================
    // NORMAL OUTPUT (for deferred rendering)
    // ========================================================================

#if @writeNormals
    fragNormal.xyz = viewNormal * 0.5 + 0.5;
    fragNormal.w = 1.0;
#endif

    applyShadowDebugOverlay();
}
