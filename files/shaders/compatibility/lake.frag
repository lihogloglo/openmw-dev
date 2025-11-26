#version 120

// Lake fragment shader with SSR + cubemap hybrid reflections
// Fixed reflection jumping by using world-space coordinates consistently
//
// Debug modes (set via debugMode uniform):
// 0 = Normal rendering (SSR + cubemap + water color)
// 1 = Solid color (verify geometry is rendering)
// 2 = World position visualization (RGB = XYZ)
// 3 = Normal visualization
// 4 = SSR only (no cubemap fallback)
// 5 = Cubemap only (no SSR)
// 6 = SSR confidence visualization (green = high confidence)
// 7 = Screen UV visualization
// 8 = Depth visualization

varying vec2 vTexCoord;
varying vec3 vWorldPos;
varying vec3 vViewPos;
varying vec4 vScreenPos;
varying float vLinearDepth;

// Texture samplers
uniform sampler2D ssrTexture;       // Unit 0: SSR result (RGB=color, A=confidence)
uniform samplerCube environmentMap; // Unit 1: Cubemap fallback
uniform sampler2D normalMap;        // Unit 2: Water normal map

// Uniforms from LakeStateSetUpdater
uniform vec2 screenRes;
uniform float osg_SimulationTime;
uniform float near;
uniform float far;
uniform int debugMode;

// Camera uniforms for reflection calculation
uniform mat4 viewMatrix;
uniform mat4 invViewMatrix;
uniform vec3 cameraPos;

// Water appearance constants
const vec3 WATER_COLOR = vec3(0.05, 0.12, 0.19);      // Deep water color
const vec3 SHALLOW_COLOR = vec3(0.1, 0.25, 0.35);     // Shallow/surface tint
const float WAVE_SCALE = 0.015;                        // Normal map wave scale (reduced for stability)
const float WAVE_SPEED = 0.02;                         // Wave animation speed
const float FRESNEL_POWER = 4.0;                       // Fresnel falloff
const float REFLECTION_STRENGTH = 0.7;                 // Base reflection amount
const float SSR_DISTORTION = 0.01;                     // SSR UV distortion amount (reduced)

// Fresnel approximation (Schlick's)
float fresnel(vec3 viewDir, vec3 normal, float f0)
{
    float cosTheta = max(dot(-viewDir, normal), 0.0);
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, FRESNEL_POWER);
}

// Sample animated water normal in world space
vec3 sampleWaterNormal(vec2 worldXY, float time)
{
    // Use world coordinates for stable normals that don't jump with camera
    // Scale based on Morrowind units (1 foot = 22.1 units)
    vec2 worldUV = worldXY / 2000.0;  // Roughly 90 foot wave repeat

    // Two layers of normal maps moving in different directions
    vec2 uv1 = worldUV * 4.0 + vec2(time * WAVE_SPEED, time * WAVE_SPEED * 0.7);
    vec2 uv2 = worldUV * 6.0 - vec2(time * WAVE_SPEED * 0.8, time * WAVE_SPEED * 0.5);

    vec3 n1 = texture2D(normalMap, uv1).rgb * 2.0 - 1.0;
    vec3 n2 = texture2D(normalMap, uv2).rgb * 2.0 - 1.0;

    // Blend normals (in tangent space, Z is up)
    vec3 normal = normalize(vec3(
        (n1.xy + n2.xy) * WAVE_SCALE,
        1.0
    ));

    return normal;
}

void main()
{
    // ============================================================
    // DEBUG MODES
    // ============================================================

    // Mode 1: Solid color - verify geometry renders
    if (debugMode == 1)
    {
        gl_FragColor = vec4(1.0, 0.0, 1.0, 0.9);  // Magenta
        return;
    }

    // Mode 9: EMERGENCY FALLBACK - Simple water without reflections
    // Use this to test if the issue is depth-related or reflection-related
    if (debugMode == 9)
    {
        // Simple water color based on view angle (basic Fresnel approximation)
        vec3 viewDir = normalize(vWorldPos - cameraPos);
        float viewAngle = abs(dot(viewDir, vec3(0.0, 0.0, 1.0)));
        vec3 waterColor = mix(vec3(0.02, 0.1, 0.15), vec3(0.05, 0.2, 0.3), viewAngle);
        gl_FragColor = vec4(waterColor, 0.75);
        return;
    }

    // Mode 2: World position visualization
    if (debugMode == 2)
    {
        // Map world coordinates to colors (scaled for Morrowind units)
        vec3 worldPosColor = fract(vWorldPos / 10000.0);
        gl_FragColor = vec4(worldPosColor, 0.9);
        return;
    }

    // Get screen UV from clip position (used by several debug modes)
    vec2 screenUV = (vScreenPos.xy / vScreenPos.w) * 0.5 + 0.5;

    // Mode 7: Screen UV visualization
    if (debugMode == 7)
    {
        gl_FragColor = vec4(screenUV, 0.0, 0.9);
        return;
    }

    // Mode 8: Depth visualization
    if (debugMode == 8)
    {
        float normalizedDepth = clamp(vLinearDepth / 10000.0, 0.0, 1.0);
        gl_FragColor = vec4(vec3(normalizedDepth), 0.9);
        return;
    }

    // ============================================================
    // MAIN RENDERING
    // ============================================================

    // Calculate view direction in world space
    vec3 viewDir = normalize(vWorldPos - cameraPos);

    // Sample animated water normal using world coordinates (fixes jumping!)
    vec3 waterNormalTangent = sampleWaterNormal(vWorldPos.xy, osg_SimulationTime);

    // Transform tangent-space normal to world space
    // For a flat water surface, tangent space Z aligns with world Z
    vec3 waterNormal = normalize(vec3(waterNormalTangent.x, waterNormalTangent.y, waterNormalTangent.z));

    // Mode 3: Normal visualization
    if (debugMode == 3)
    {
        gl_FragColor = vec4(waterNormal * 0.5 + 0.5, 0.9);
        return;
    }

    // Calculate reflection direction in world space
    vec3 reflectDir = reflect(viewDir, waterNormal);

    // Calculate Fresnel term
    float fresnelTerm = fresnel(viewDir, waterNormal, 0.02);

    // ============================================================
    // SSR SAMPLING
    // ============================================================
    // Add slight distortion based on water normal for more realistic look
    vec2 ssrUV = screenUV + waterNormalTangent.xy * SSR_DISTORTION;
    ssrUV = clamp(ssrUV, 0.001, 0.999);  // Keep within texture bounds

    vec4 ssrSample = texture2D(ssrTexture, ssrUV);
    float ssrConfidence = ssrSample.a;

    // Mode 6: SSR confidence visualization
    if (debugMode == 6)
    {
        gl_FragColor = vec4(0.0, ssrConfidence, 0.0, 0.9);
        return;
    }

    // Mode 4: SSR only
    if (debugMode == 4)
    {
        gl_FragColor = vec4(ssrSample.rgb, 0.9);
        return;
    }

    // ============================================================
    // CUBEMAP SAMPLING
    // ============================================================
    // Cubemap is in world space, so reflectDir can be used directly
    vec3 cubemapColor = textureCube(environmentMap, reflectDir).rgb;

    // Mode 5: Cubemap only
    if (debugMode == 5)
    {
        gl_FragColor = vec4(cubemapColor, 0.9);
        return;
    }

    // ============================================================
    // BLEND SSR AND CUBEMAP
    // ============================================================
    vec3 reflection;
    if (ssrConfidence > 0.05)
    {
        // Smooth blend based on confidence
        float blendFactor = smoothstep(0.05, 0.4, ssrConfidence);
        reflection = mix(cubemapColor, ssrSample.rgb, blendFactor);
    }
    else
    {
        // Pure cubemap fallback
        reflection = cubemapColor;
    }

    // ============================================================
    // FINAL COLOR COMPOSITION
    // ============================================================
    // Base water color
    vec3 waterColor = mix(WATER_COLOR, SHALLOW_COLOR, 0.3);

    // Mix water color and reflection based on Fresnel
    float reflectionAmount = fresnelTerm * REFLECTION_STRENGTH;
    vec3 finalColor = mix(waterColor, reflection, reflectionAmount);

    // Add subtle specular highlight
    float sunDir = max(0.0, dot(reflectDir, normalize(vec3(0.3, 0.2, 1.0))));
    float specHighlight = pow(sunDir, 64.0) * 0.2;
    finalColor += vec3(specHighlight);

    // Output with transparency
    // Alpha varies with Fresnel (more transparent when looking down)
    float alpha = mix(0.5, 0.92, fresnelTerm);

    gl_FragColor = vec4(finalColor, alpha);
}
