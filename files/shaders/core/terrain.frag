#version 400 core

// ============================================================================
// TERRAIN TESSELLATION FRAGMENT SHADER (Core Profile)
// ============================================================================
// Handles lighting, texturing, and visual effects for tessellated terrain.
// Receives interpolated data from the tessellation evaluation shader.
// ============================================================================

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
    float tessLevel;  // Debug: tessellation level
} fs_in;

// Outputs
layout(location = 0) out vec4 fragColor;
#if @writeNormals
layout(location = 1) out vec4 fragNormal;
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

// Material uniforms
uniform vec4 diffuseColor;
uniform vec4 ambientColor;
uniform vec4 emissionColor;
uniform vec4 specularColor;
uniform float shininess;

// Environment uniforms
uniform float far;
uniform mat4 osg_ViewMatrixInverse;
uniform mat3 osg_NormalMatrix;

// Texture matrices (for tiling)
uniform mat4 textureMatrix0;
uniform mat4 textureMatrix1;

// ============================================================================
// LIGHTING (Simplified for initial implementation)
// ============================================================================

// Light uniforms (simplified - you'll want to integrate with OpenMW's lighting system)
uniform vec3 sunDirection;  // Sun direction in view space
uniform vec3 sunColor;
uniform vec3 ambientLight;

vec3 calculateLighting(vec3 normal, vec3 viewDir, vec3 baseColor)
{
    // Simple diffuse + ambient lighting
    float NdotL = max(dot(normal, -sunDirection), 0.0);
    vec3 diffuse = baseColor * sunColor * NdotL;
    vec3 ambient = baseColor * ambientLight;

    // Simple specular (Blinn-Phong)
    vec3 halfDir = normalize(-sunDirection + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
    vec3 specular = specularColor.rgb * sunColor * spec;

    return diffuse + ambient + specular + emissionColor.rgb;
}

// ============================================================================
// FOG (Simplified)
// ============================================================================

uniform vec4 fogColor;
uniform float fogStart;
uniform float fogEnd;

vec4 applyFog(vec4 color, float distance)
{
    float fogFactor = clamp((fogEnd - distance) / (fogEnd - fogStart), 0.0, 1.0);
    return vec4(mix(fogColor.rgb, color.rgb, fogFactor), color.a);
}

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

    // Get base diffuse color
    vec4 matDiffuse = diffuseColor * fs_in.color;

    // ========================================================================
    // SNOW POM & DARKENING (Parallax Occlusion Mapping for footprints)
    // ========================================================================

    float deformationFactor = fs_in.deformationFactor;

    if (fs_in.maxDepth > 0.0)
    {
        // Calculate view vector in world space
        vec3 viewDir = normalize((osg_ViewMatrixInverse * vec4(fs_in.viewPos, 0.0)).xyz);

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

    // Apply visual effects for deformed areas
    float darkeningFactor;
    if (deformationFactor > 0.0)
    {
        darkeningFactor = smoothstep(0.0, 1.0, deformationFactor) * 0.5;
    }
    else
    {
        darkeningFactor = deformationFactor * 0.15;
    }

    matDiffuse.rgb *= (1.0 - darkeningFactor);
    fragColor.a *= matDiffuse.a;

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
    viewNormal = normalize(osg_NormalMatrix * vec3(tangentNormal.xy, tangentNormal.z));
#else
    viewNormal = normalize(osg_NormalMatrix * fs_in.normal);
#endif

    // Apply snow deformation normal perturbation
    if (fs_in.maxDepth > 0.0)
    {
        vec2 deformUV = (fs_in.worldPos.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
        if (deformUV.x >= 0.0 && deformUV.x <= 1.0 && deformUV.y >= 0.0 && deformUV.y <= 1.0)
        {
            float texelSize = 2.0 / 2048.0;

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
            vec3 viewPerturb = normalize(osg_NormalMatrix * worldPerturb);
            vec3 viewUp = normalize(osg_NormalMatrix * vec3(0.0, 0.0, 1.0));
            vec3 diff = viewPerturb - viewUp;

            viewNormal = normalize(viewNormal + diff);
        }
    }

    // ========================================================================
    // LIGHTING
    // ========================================================================

    vec3 viewDir = normalize(-fs_in.viewPos);
    vec3 lighting = calculateLighting(viewNormal, viewDir, matDiffuse.rgb);

    fragColor.rgb = fragColor.rgb * lighting;

    // ========================================================================
    // FOG
    // ========================================================================

    fragColor = applyFog(fragColor, fs_in.euclideanDepth);

    // ========================================================================
    // DEBUG: TESSELLATION LEVEL VISUALIZATION
    // ========================================================================
    // Tint terrain by tessellation level: blue = low (1), red = high (16)
    // Remove or comment out this block when done debugging
    float tessNormalized = clamp((fs_in.tessLevel - 1.0) / 15.0, 0.0, 1.0);  // Normalize 1-16 to 0-1
    vec3 tessDebugColor = mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), tessNormalized);  // Blue to Red
    fragColor.rgb = mix(fragColor.rgb, tessDebugColor, 0.4);  // 40% tint

    // ========================================================================
    // NORMAL OUTPUT (for deferred rendering)
    // ========================================================================

#if @writeNormals
    fragNormal.xyz = viewNormal * 0.5 + 0.5;
    fragNormal.w = 1.0;
#endif
}
