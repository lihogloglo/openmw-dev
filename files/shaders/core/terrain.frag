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
    vec3 worldPos;
    float deformationFactor;
    float maxDepth;
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
uniform sampler2D snowDeformationMap;

// Deformation uniforms
uniform vec3 snowRTTWorldOrigin;
uniform float snowRTTScale;
uniform float deformationMapResolution; // Resolution of deformation map (1024, 2048, or 4096)

// Environment uniforms
uniform float far;
uniform vec2 screenRes;

// Texture matrices (for tiling)
uniform mat4 textureMatrix0;
uniform mat4 textureMatrix1;

// Define PER_PIXEL_LIGHTING for lighting.glsl
#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

// Color mode uniform (for terrain, always use material colors)
uniform int colorMode;
const int ColorMode_None = 0;
const int ColorMode_Emission = 1;
const int ColorMode_AmbientAndDiffuse = 2;
const int ColorMode_Ambient = 3;
const int ColorMode_Diffuse = 4;
const int ColorMode_Specular = 5;

// Color functions - use fs_in.color or gl_FrontMaterial based on colorMode
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

    // ========================================================================
    // SNOW POM & DARKENING (Parallax Occlusion Mapping for footprints)
    // ========================================================================

    float deformationFactor = fs_in.deformationFactor;

    if (fs_in.maxDepth > 0.0)
    {
        // Calculate view vector in world space using compatibility profile inverse view matrix
        mat4 viewMatrixInverse = gl_ModelViewMatrixInverse;
        vec3 viewDir = normalize((viewMatrixInverse * vec4(fs_in.viewPos, 0.0)).xyz);

        // Calculate undeformed snow surface height
        float flatZ = fs_in.worldPos.z + fs_in.deformationFactor * fs_in.maxDepth;

        vec3 p = fs_in.worldPos;
        vec3 v = viewDir;

        // Raymarch parameters
        const int STEPS = 10;
        float stepSize = fs_in.maxDepth * 0.15;

        // Back up starting point
        p -= v * stepSize * 2.0;

        for (int i = 0; i < STEPS; ++i)
        {
            p += v * stepSize;

            // Calculate RTT UV
            vec2 rttUV = (p.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;

            if (rttUV.x >= 0.0 && rttUV.x <= 1.0 && rttUV.y >= 0.0 && rttUV.y <= 1.0)
            {
                float r = texture(snowDeformationMap, rttUV).r;
                float surfaceH = flatZ - r * fs_in.maxDepth;

                if (p.z < surfaceH)
                {
                    deformationFactor = r;
                    break;
                }
            }
        }
    }

    // Calculate visual effects for deformed areas (used later in lighting)
    float darkeningFactor = 0.0;
    if (deformationFactor > 0.0)
    {
        darkeningFactor = smoothstep(0.0, 1.0, deformationFactor) * 0.5;
    }
    else
    {
        darkeningFactor = deformationFactor * 0.15;
    }

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
    // TODO: Proper TBN matrix calculation for terrain
    // For now, assume terrain is mostly flat and use simplified transform
    viewNormal = normalize(gl_NormalMatrix * vec3(tangentNormal.xy, tangentNormal.z));
#else
    viewNormal = normalize(gl_NormalMatrix * fs_in.normal);
#endif

    // Apply snow deformation normal perturbation
    if (fs_in.maxDepth > 0.0)
    {
        vec2 deformUV = (fs_in.worldPos.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
        if (deformUV.x >= 0.0 && deformUV.x <= 1.0 && deformUV.y >= 0.0 && deformUV.y <= 1.0)
        {
            float resolution = deformationMapResolution > 0.0 ? deformationMapResolution : 2048.0;
            float texelSize = 2.0 / resolution;

            // Central difference for normals
            float h_l = texture(snowDeformationMap, deformUV + vec2(-texelSize, 0.0)).r;
            float h_r = texture(snowDeformationMap, deformUV + vec2(texelSize, 0.0)).r;
            float h_d = texture(snowDeformationMap, deformUV + vec2(0.0, -texelSize)).r;
            float h_u = texture(snowDeformationMap, deformUV + vec2(0.0, texelSize)).r;

            float stepWorld = snowRTTScale * texelSize * 2.0;
            float dX = (h_l - h_r) * fs_in.maxDepth / stepWorld;
            float dY = (h_d - h_u) * fs_in.maxDepth / stepWorld;

            const float normalStrength = 1.5;
            dX *= normalStrength;
            dY *= normalStrength;

            vec3 worldPerturb = normalize(vec3(-dX, -dY, 1.0));
            vec3 viewPerturb = normalize(gl_NormalMatrix * worldPerturb);
            vec3 viewUp = normalize(gl_NormalMatrix * vec3(0.0, 0.0, 1.0));
            vec3 diff = viewPerturb - viewUp;

            viewNormal = normalize(viewNormal + diff);
        }
    }

    // ========================================================================
    // LIGHTING - Using OpenMW's lighting system
    // ========================================================================

    float shadowing = unshadowedLightRatio(fs_in.linearDepth);
    vec3 lighting, specular;

#if PER_PIXEL_LIGHTING
    #if @specularMap
        float materialShininess = 128.0; // TODO: make configurable
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
    // Vertex lighting fallback (simplified for tessellation)
    vec3 diffuseLight, ambientLight, specularLight;
    vec3 shadowDiffuse, shadowSpecular;
    doLighting(fs_in.viewPos, viewNormal, gl_FrontMaterial.shininess, diffuseLight, ambientLight, specularLight, shadowDiffuse, shadowSpecular);
    lighting = getDiffuseColor().xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    lighting += shadowDiffuse * shadowing;
    specular = getSpecularColor().xyz * specularLight + shadowSpecular * shadowing;
#endif

    // Apply darkening from deformation to diffuse
    lighting *= (1.0 - darkeningFactor);

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
