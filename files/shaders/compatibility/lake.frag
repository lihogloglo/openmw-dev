#version 120

// Lake fragment shader with SSR + cubemap hybrid reflections

varying vec2 vTexCoord;
varying vec3 vWorldPos;
varying vec4 vScreenPos;
varying float vLinearDepth;

// Texture samplers
uniform sampler2D ssrTexture;      // Unit 0: SSR result (RGB=color, A=confidence)
uniform samplerCube environmentMap; // Unit 1: Cubemap fallback
uniform sampler2D normalMap;        // Unit 2: Water normal map

// Uniforms
uniform vec2 screenRes;
uniform float osg_SimulationTime;
uniform float near;
uniform float far;

// Water appearance constants
const vec3 WATER_COLOR = vec3(0.05, 0.12, 0.19);      // Deep water color
const vec3 SHALLOW_COLOR = vec3(0.1, 0.25, 0.35);     // Shallow/surface tint
const float WAVE_SCALE = 0.02;                         // Normal map wave scale
const float WAVE_SPEED = 0.03;                         // Wave animation speed
const float FRESNEL_POWER = 4.0;                       // Fresnel falloff
const float REFLECTION_STRENGTH = 0.6;                 // Base reflection amount
const float SSR_DISTORTION = 0.02;                     // SSR UV distortion amount

// Simplified Fresnel approximation (Schlick's)
float fresnel(vec3 viewDir, vec3 normal, float f0)
{
    float cosTheta = max(dot(-viewDir, normal), 0.0);
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, FRESNEL_POWER);
}

// Sample animated water normal
vec3 sampleWaterNormal(vec2 uv, float time)
{
    // Two layers of normal maps moving in different directions
    vec2 uv1 = uv * 8.0 + vec2(time * WAVE_SPEED, time * WAVE_SPEED * 0.7);
    vec2 uv2 = uv * 12.0 - vec2(time * WAVE_SPEED * 0.8, time * WAVE_SPEED * 0.5);

    vec3 n1 = texture2D(normalMap, uv1).rgb * 2.0 - 1.0;
    vec3 n2 = texture2D(normalMap, uv2).rgb * 2.0 - 1.0;

    // Blend normals
    vec3 normal = normalize(vec3(
        (n1.xy + n2.xy) * WAVE_SCALE,
        1.0
    ));

    return normal;
}

void main()
{
    // Get screen UV from clip position
    vec2 screenUV = (vScreenPos.xy / vScreenPos.w) * 0.5 + 0.5;

    // Calculate view direction (in view space since vWorldPos is view space)
    vec3 viewDir = normalize(vWorldPos);

    // Sample animated water normal
    vec3 waterNormal = sampleWaterNormal(vTexCoord * 20.0, osg_SimulationTime);

    // Transform normal to view space (simplified - assuming flat water surface)
    vec3 viewNormal = normalize(gl_NormalMatrix * vec3(waterNormal.x, waterNormal.y, 1.0));

    // Calculate reflection direction for cubemap
    vec3 reflectDir = reflect(viewDir, viewNormal);

    // Calculate Fresnel term
    float fresnelTerm = fresnel(viewDir, viewNormal, 0.02);

    // --- SSR Sampling ---
    // Add slight distortion based on water normal for more realistic look
    vec2 ssrUV = screenUV + waterNormal.xy * SSR_DISTORTION;
    ssrUV = clamp(ssrUV, 0.001, 0.999);  // Keep within texture bounds

    vec4 ssrSample = texture2D(ssrTexture, ssrUV);
    float ssrConfidence = ssrSample.a;

    // --- Cubemap Sampling ---
    // Transform reflect direction back to world space for cubemap lookup
    // (cubemap is in world space, our reflect dir is in view space)
    vec3 worldReflectDir = (gl_ModelViewMatrixInverse * vec4(reflectDir, 0.0)).xyz;
    vec3 cubemapColor = textureCube(environmentMap, worldReflectDir).rgb;

    // --- Blend SSR and Cubemap ---
    // Use SSR when confidence is high, cubemap as fallback
    vec3 reflection;
    if (ssrConfidence > 0.1)
    {
        // Smooth blend based on confidence
        float blendFactor = smoothstep(0.1, 0.5, ssrConfidence);
        reflection = mix(cubemapColor, ssrSample.rgb, blendFactor);
    }
    else
    {
        // Pure cubemap fallback
        reflection = cubemapColor;
    }

    // --- Final Color Composition ---
    // Base water color with depth-based tint
    vec3 waterColor = mix(WATER_COLOR, SHALLOW_COLOR, 0.3);

    // Mix water color and reflection based on Fresnel
    float reflectionAmount = fresnelTerm * REFLECTION_STRENGTH;
    vec3 finalColor = mix(waterColor, reflection, reflectionAmount);

    // Add subtle specular highlight based on reflection brightness
    float specHighlight = max(0.0, dot(worldReflectDir, vec3(0.0, 0.0, 1.0)));
    specHighlight = pow(specHighlight, 64.0) * 0.3;
    finalColor += vec3(specHighlight);

    // Output with transparency for water blending
    // Alpha decreases with Fresnel (more transparent when looking down)
    float alpha = mix(0.6, 0.95, fresnelTerm);

    gl_FragColor = vec4(finalColor, alpha);
}
