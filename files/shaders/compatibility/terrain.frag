#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

varying vec2 uv;
varying float vDeformationFactor; // From vertex shader
varying vec3 passWorldPos;          // From vertex shader
varying float vMaxDepth;            // From vertex shader

uniform sampler2D snowDeformationMap;
uniform vec3 snowRTTWorldOrigin;
uniform float snowRTTScale;
uniform int deformationDebugMode; // 0=off, 1=UV coords, 2=deform value, 3=world offset

uniform sampler2D diffuseMap;

#if @normalMap
uniform sampler2D normalMap;
#endif

#if @blendMap
uniform sampler2D blendMap;
#endif

varying float euclideanDepth;
varying float linearDepth;

#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

#if !PER_PIXEL_LIGHTING
centroid varying vec3 passLighting;
centroid varying vec3 passSpecular;
centroid varying vec3 shadowDiffuseLighting;
centroid varying vec3 shadowSpecularLighting;
#endif
varying vec3 passViewPos;
varying vec3 passNormal;

uniform vec2 screenRes;
uniform float far;

#include "vertexcolors.glsl"
#include "shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "lib/material/parallax.glsl"
#include "fog.glsl"
#include "compatibility/normals.glsl"

void main()
{
    vec2 adjustedUV = (gl_TextureMatrix[0] * vec4(uv, 0.0, 1.0)).xy;

#if @parallax
    float height = texture2D(normalMap, adjustedUV).a;
    adjustedUV += getParallaxOffset(transpose(normalToViewMatrix) * normalize(-passViewPos), height);
#endif
    vec4 diffuseTex = texture2D(diffuseMap, adjustedUV);
    gl_FragData[0] = vec4(diffuseTex.xyz, 1.0);

    vec4 diffuseColor = getDiffuseColor();
    
    // ========================================================================
    // SNOW POM & DARKENING
    // ========================================================================
    
    float deformationFactor = vDeformationFactor;
    
    if (vMaxDepth > 0.0)
    {
        // 1. Calculate View Vector in World Space
        // passViewPos is vector from Camera to Vertex in View Space
        // We need it in World Space. Assuming standard OpenMW View matrix (Rotation only for normals)
        // viewDirWorld = inverse(View) * viewDirView
        // We use gl_ModelViewMatrixInverse because for terrain chunks Model is just translation
        vec3 viewDir = normalize((gl_ModelViewMatrixInverse * vec4(passViewPos, 0.0)).xyz);
        
        // 2. Raymarch Setup
        // Calculate the undeformed snow surface height
        // passWorldPos.z is already deformed, so we add back the deformation to get the original flat surface
        float flatZ = passWorldPos.z + vDeformationFactor * vMaxDepth;
        
        vec3 p = passWorldPos;
        vec3 v = viewDir;
        
        // Raymarch parameters
        const int STEPS = 10;
        float stepSize = vMaxDepth * 0.15; // Step size relative to max depth
        
        // Refine starting point: back up a bit to ensure we don't start inside
        p -= v * stepSize * 2.0;
        
        for (int i = 0; i < STEPS; ++i)
        {
            p += v * stepSize;
            
            // Calculate RTT UV
            vec2 rttUV = (p.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
            
            // Check bounds
            if (rttUV.x >= 0.0 && rttUV.x <= 1.0 && rttUV.y >= 0.0 && rttUV.y <= 1.0)
            {
                float r = texture2D(snowDeformationMap, rttUV).r;
                
                // Current depth of the hole at this point
                // Surface is at flatZ. Hole bottom is at flatZ - vMaxDepth.
                // Actual surface height = flatZ - r * vMaxDepth.
                float surfaceH = flatZ - r * vMaxDepth;
                
                // If ray is below surface, we hit
                if (p.z < surfaceH)
                {
                    deformationFactor = r;
                    
                    // TODO: Calculate UV offset for texture shifting
                    // For now, we just use the updated deformation factor for correct darkening
                    break;
                }
            }
        }
    }

    // Apply visual effects for deformed areas (wet/compressed snow/mud)
    // deformationFactor can be:
    //   positive (0 to 1) = depression (footprint center) - darken more
    //   negative (-0.5 to 0) = rim elevation - slight brightening
    //   zero = flat snow

    float darkeningFactor;
    if (deformationFactor > 0.0)
    {
        // Depression: darken progressively (compressed/wet snow appearance)
        // Stronger darkening for deeper impressions using smoothstep
        darkeningFactor = smoothstep(0.0, 1.0, deformationFactor) * 0.5;
    }
    else
    {
        // Rim: very slight brightening (snow pushed up catches more light)
        darkeningFactor = deformationFactor * 0.15; // Negative = brightening
    }

    diffuseColor.rgb *= (1.0 - darkeningFactor);

    gl_FragData[0].a *= diffuseColor.a;

#if @blendMap
    vec2 blendMapUV = (gl_TextureMatrix[1] * vec4(uv, 0.0, 1.0)).xy;
    gl_FragData[0].a *= texture2D(blendMap, blendMapUV).a;
#endif

#if @normalMap
    vec4 normalTex = texture2D(normalMap, adjustedUV);
    vec3 normal = normalTex.xyz * 2.0 - 1.0;
#if @reconstructNormalZ
    normal.z = sqrt(1.0 - dot(normal.xy, normal.xy));
#endif
    // If snow deformation is active, perturb the normal map normal
    if (vMaxDepth > 0.0)
    {
        vec2 deformUV = (passWorldPos.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
        if (deformUV.x >= 0.0 && deformUV.x <= 1.0 && deformUV.y >= 0.0 && deformUV.y <= 1.0)
        {
            float texelSize = 1.0 / 2048.0;
            float h = texture2D(snowDeformationMap, deformUV).r;
            float h_r = texture2D(snowDeformationMap, deformUV + vec2(texelSize, 0.0)).r;
            float h_u = texture2D(snowDeformationMap, deformUV + vec2(0.0, texelSize)).r;
            
            // Calculate derivatives
            // Z = Base - h * MaxDepth
            // dZ/dX = (Z_r - Z) / step = ((-h_r) - (-h)) * MaxDepth / step = (h - h_r) * MaxDepth / step
            float stepWorld = snowRTTScale * texelSize;
            float dX = (h - h_r) * vMaxDepth / stepWorld;
            float dY = (h - h_u) * vMaxDepth / stepWorld;
            
            // Perturb the tangent-space normal? 
            // No, 'normal' here is Tangent Space (from normal map).
            // We calculated dX, dY in World Space (assuming Terrain UV aligns with World XY).
            // We need to convert World Space perturbation to Tangent Space?
            // Or just perturb the View Space normal later?
            // Perturbing View Space is easier if we don't have TBN here.
            // But we DO have TBN implicit in 'normalToView' if normalMap is on?
            // Actually, 'normalToView' usually multiplies by TBN.
            // Let's perturb the RESULT of normalToView.
        }
    }
    vec3 viewNormal = normalToView(normal);
#else
    vec3 viewNormal = normalize(gl_NormalMatrix * passNormal);
#endif

    // Apply Snow Deformation Normal Perturbation (Post-NormalMap / Post-VertexNormal)
    // This creates the lighting response that makes footprints look 3D
    if (vMaxDepth > 0.0)
    {
        vec2 deformUV = (passWorldPos.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
        if (deformUV.x >= 0.0 && deformUV.x <= 1.0 && deformUV.y >= 0.0 && deformUV.y <= 1.0)
        {
            // Use larger sample offset for smoother normals (matches blur spread)
            float texelSize = 2.0 / 2048.0; // 2x texel for smoother gradients

            // Central difference for more accurate normals
            float h_l = texture2D(snowDeformationMap, deformUV + vec2(-texelSize, 0.0)).r;
            float h_r = texture2D(snowDeformationMap, deformUV + vec2(texelSize, 0.0)).r;
            float h_d = texture2D(snowDeformationMap, deformUV + vec2(0.0, -texelSize)).r;
            float h_u = texture2D(snowDeformationMap, deformUV + vec2(0.0, texelSize)).r;

            // Calculate gradients using central difference
            float stepWorld = snowRTTScale * texelSize * 2.0; // Full span = 2 * texelSize
            float dX = (h_l - h_r) * vMaxDepth / stepWorld;
            float dY = (h_d - h_u) * vMaxDepth / stepWorld;

            // Amplify normal strength for more dramatic lighting
            const float normalStrength = 1.5;
            dX *= normalStrength;
            dY *= normalStrength;

            // Construct perturbation vector in World Space (Z-up)
            // Normal = cross(tangentX, tangentY) = (-dX, -dY, 1)
            vec3 worldPerturb = normalize(vec3(-dX, -dY, 1.0));

            // Convert to View Space
            vec3 viewPerturb = normalize(gl_NormalMatrix * worldPerturb);

            // Get the world-up direction in view space for blending
            vec3 viewUp = normalize(gl_NormalMatrix * vec3(0.0, 0.0, 1.0));

            // Calculate the perturbation as deviation from world-up
            vec3 diff = viewPerturb - viewUp;

            // Apply perturbation to existing normal
            // This correctly combines terrain slope with footprint shape
            viewNormal = normalize(viewNormal + diff);
        }
    }

    float shadowing = unshadowedLightRatio(linearDepth);
    vec3 lighting, specular;
#if !PER_PIXEL_LIGHTING
    lighting = passLighting + shadowDiffuseLighting * shadowing;
    specular = passSpecular + shadowSpecularLighting * shadowing;
#else
#if @specularMap
    float shininess = 128.0; // TODO: make configurable
    vec3 specularColor = vec3(diffuseTex.a);
#else
    float shininess = gl_FrontMaterial.shininess;
    vec3 specularColor = getSpecularColor().xyz;
#endif
    vec3 diffuseLight, ambientLight, specularLight;
    doLighting(passViewPos, viewNormal, shininess, shadowing, diffuseLight, ambientLight, specularLight);
    lighting = diffuseColor.xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    specular = specularColor * specularLight;
#endif

    clampLightingResult(lighting);
    gl_FragData[0].xyz = gl_FragData[0].xyz * lighting + specular;

    gl_FragData[0] = applyFogAtDist(gl_FragData[0], euclideanDepth, linearDepth, far);

#if !@disableNormals && @writeNormals
    gl_FragData[1].xyz = viewNormal * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();

    // ========================================================================
    // DEBUG VISUALIZATION FOR DEFORMATION SYSTEM
    // ========================================================================
    // Mode 1: Terrain UV coords - verify shader's UV calculation
    // Mode 2: Raw deformation map sample - see what's in the texture
    // Mode 3: World position debug - verify passWorldPos is correct
    // Mode 4: Cardinal direction test - RED=East, GREEN=North, CYAN=West, MAGENTA=South
    // Mode 5: Deformation map with crosshair at center
    // ========================================================================
    if (deformationDebugMode > 0)
    {
        vec2 deformUV = (passWorldPos.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;
        bool inBounds = (deformUV.x >= 0.0 && deformUV.x <= 1.0 && deformUV.y >= 0.0 && deformUV.y <= 1.0);

        if (deformationDebugMode == 1)
        {
            // Mode 1: Show calculated UV coordinates
            // Expected: Orange (0.5, 0.5) at player position
            // Red increases East (+X world), Green increases North (+Y world)
            if (inBounds)
                gl_FragData[0].rgb = vec3(deformUV.x, deformUV.y, 0.0);
            else
                gl_FragData[0].rgb = vec3(0.0, 0.0, 1.0);
        }
        else if (deformationDebugMode == 2)
        {
            // Mode 2: Show raw deformation map value
            if (inBounds)
            {
                float deformValue = texture2D(snowDeformationMap, deformUV).r;
                gl_FragData[0].rgb = vec3(deformValue);
            }
            else
            {
                gl_FragData[0].rgb = vec3(0.0, 0.0, 1.0);
            }
        }
        else if (deformationDebugMode == 3)
        {
            // Mode 3: Show passWorldPos relative to origin
            // This tests if chunkWorldOffset is correct
            vec2 relPos = passWorldPos.xy - snowRTTWorldOrigin.xy;
            // Normalize to visible range (divide by expected range ~1800 units)
            gl_FragData[0].rgb = vec3(
                relPos.x / 1800.0 + 0.5,
                relPos.y / 1800.0 + 0.5,
                0.0
            );
        }
        else if (deformationDebugMode == 4)
        {
            // Mode 4: Cardinal direction color test
            // Stand at origin, look around:
            // - EAST  (+X from player): Should be RED
            // - NORTH (+Y from player): Should be GREEN
            // - WEST  (-X from player): Should be CYAN
            // - SOUTH (-Y from player): Should be MAGENTA
            vec2 relPos = passWorldPos.xy - snowRTTWorldOrigin.xy;
            float dist = length(relPos);

            if (dist < 100.0)
            {
                // Player position marker - WHITE
                gl_FragData[0].rgb = vec3(1.0, 1.0, 1.0);
            }
            else
            {
                // Determine dominant direction
                float absX = abs(relPos.x);
                float absY = abs(relPos.y);

                if (absX > absY)
                {
                    // East-West axis dominant
                    if (relPos.x > 0.0)
                        gl_FragData[0].rgb = vec3(1.0, 0.0, 0.0); // EAST = RED
                    else
                        gl_FragData[0].rgb = vec3(0.0, 1.0, 1.0); // WEST = CYAN
                }
                else
                {
                    // North-South axis dominant
                    if (relPos.y > 0.0)
                        gl_FragData[0].rgb = vec3(0.0, 1.0, 0.0); // NORTH = GREEN
                    else
                        gl_FragData[0].rgb = vec3(1.0, 0.0, 1.0); // SOUTH = MAGENTA
                }
            }
        }
        else if (deformationDebugMode == 5)
        {
            // Mode 5: Show deformation map with crosshair overlay
            // Crosshair shows where UV (0.5, 0.5) is - should be at player feet
            if (inBounds)
            {
                float deformValue = texture2D(snowDeformationMap, deformUV).r;
                vec3 baseColor = vec3(deformValue);

                // Draw crosshair at center (UV 0.5, 0.5)
                float crossSize = 0.02;
                float lineWidth = 0.005;
                bool onVertical = (abs(deformUV.x - 0.5) < lineWidth) && (abs(deformUV.y - 0.5) < crossSize);
                bool onHorizontal = (abs(deformUV.y - 0.5) < lineWidth) && (abs(deformUV.x - 0.5) < crossSize);

                if (onVertical || onHorizontal)
                    gl_FragData[0].rgb = vec3(1.0, 1.0, 0.0); // Yellow crosshair
                else
                    gl_FragData[0].rgb = baseColor;
            }
            else
            {
                gl_FragData[0].rgb = vec3(0.0, 0.0, 1.0);
            }
        }
        else if (deformationDebugMode == 6)
        {
            // Mode 6: Sample deformation map at FIXED UV positions to see what's actually in it
            // This bypasses the world-to-UV transform to test the texture directly
            // Uses screen position to sweep through the texture
            vec2 screenUV = gl_FragCoord.xy / screenRes;
            float deformValue = texture2D(snowDeformationMap, screenUV).r;
            gl_FragData[0].rgb = vec3(deformValue);
        }
        else if (deformationDebugMode == 7)
        {
            // Mode 7: Show ALL channels of deformation map (RGBA)
            if (inBounds)
            {
                vec4 deformSample = texture2D(snowDeformationMap, deformUV);
                gl_FragData[0].rgb = deformSample.rgb;
            }
            else
            {
                gl_FragData[0].rgb = vec3(0.0, 0.0, 1.0);
            }
        }
        else if (deformationDebugMode == 8)
        {
            // Mode 8: Show raw passWorldPos.xy as colors (mod 1000 for visibility)
            // This shows the actual world coordinates the shader is receiving
            float wx = mod(passWorldPos.x, 1000.0) / 1000.0;
            float wy = mod(passWorldPos.y, 1000.0) / 1000.0;
            gl_FragData[0].rgb = vec3(wx, wy, 0.0);
        }
        else if (deformationDebugMode == 9)
        {
            // Mode 9: Show snowRTTWorldOrigin.xy (player pos) as colors
            float ox = mod(snowRTTWorldOrigin.x, 1000.0) / 1000.0;
            float oy = mod(snowRTTWorldOrigin.y, 1000.0) / 1000.0;
            gl_FragData[0].rgb = vec3(ox, oy, 0.0);
        }
        else if (deformationDebugMode == 10)
        {
            // Mode 10: Simple quadrant test with gradient
            // Shows the sign and magnitude of relPos.x and relPos.y
            vec2 relPos = passWorldPos.xy - snowRTTWorldOrigin.xy;

            // Normalize to 0-1 range (assuming max distance ~2000 units)
            float normX = clamp(relPos.x / 2000.0 + 0.5, 0.0, 1.0);
            float normY = clamp(relPos.y / 2000.0 + 0.5, 0.0, 1.0);

            // R = X position (0=west, 0.5=center, 1=east if X is east)
            // G = Y position (0=south, 0.5=center, 1=north if Y is north)
            gl_FragData[0].rgb = vec3(normX, normY, 0.0);
        }
        else if (deformationDebugMode == 11)
        {
            // Mode 11: Show chunkWorldOffset itself
            // This verifies the uniform is being passed correctly
            float cx = mod(chunkWorldOffset.x, 1000.0) / 1000.0;
            float cy = mod(chunkWorldOffset.y, 1000.0) / 1000.0;
            gl_FragData[0].rgb = vec3(cx, cy, 0.0);
        }
        else if (deformationDebugMode == 12)
        {
            // Mode 12: Show depth from object mask (G channel)
            // Visualizes the RTT camera's depth capture:
            //   Black = no object
            //   Dark green = object deep below ground (shouldn't happen)
            //   Bright green = object at ground level (feet)
            //   Yellow tint = R channel showing object presence
            if (inBounds)
            {
                vec4 maskSample = texture2D(snowDeformationMap, deformUV);
                // Show R (presence) and G (depth) channels
                gl_FragData[0].rgb = vec3(maskSample.r * 0.3, maskSample.g, 0.0);
            }
            else
            {
                gl_FragData[0].rgb = vec3(0.0, 0.0, 1.0);
            }
        }
        else if (deformationDebugMode == 13)
        {
            // Mode 13: Show raw object mask (before accumulation/blur)
            // This samples directly from the depth camera output
            // Useful to see what the camera is capturing THIS frame
            if (inBounds)
            {
                vec4 maskSample = texture2D(snowDeformationMap, deformUV);
                // R = object present, G = depth, show as color
                gl_FragData[0].rgb = maskSample.rgb;
            }
            else
            {
                gl_FragData[0].rgb = vec3(0.0, 0.0, 1.0);
            }
        }
    }
}
