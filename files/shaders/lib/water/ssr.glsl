#ifndef LIB_WATER_SSR
#define LIB_WATER_SSR

// Screen-Space Reflections library for water rendering
// Compatible with both Ocean and Lake shaders
// Based on Godot's optimized raymarch algorithm

// SSR Configuration constants
const float SSR_MAX_DISTANCE = 4096.0;   // Maximum ray travel distance
const int SSR_MAX_STEPS = 16;            // Maximum raymarching iterations
const float SSR_MAX_DIFF = 300.0;        // Maximum depth difference for hit detection
const float SSR_FADE_START = 0.15;       // Screen edge fade start (fraction from edge)

// Screen edge fadeout function (Godot-style)
// Returns 0 at screen edges, 1 at center
float ssrScreenEdgeFade(vec2 screenCoords)
{
    // Calculate distance from screen edges
    vec2 edgeDist = min(screenCoords, 1.0 - screenCoords);

    // Smooth falloff near edges
    float fadeX = smoothstep(0.0, SSR_FADE_START, edgeDist.x);
    float fadeY = smoothstep(0.0, SSR_FADE_START, edgeDist.y);

    return fadeX * fadeY;
}

// Linearize depth from normalized device coordinates
// Note: Uses the same formula as lib/view/depth.glsl but without preprocessor
float ssrLinearizeDepth(float depth, float near, float far, bool reverseZ)
{
    if (reverseZ)
        depth = 1.0 - depth;

    float z_n = 2.0 * depth - 1.0;
    return 2.0 * near * far / (far + near - z_n * (far - near));
}

// Main SSR raymarching function
// Returns: vec4(color.rgb, confidence) where confidence is 0-1
//
// Parameters:
//   viewPos      - Fragment position in view space
//   viewNormal   - Surface normal in view space
//   projMatrix   - Projection matrix for screen-space conversion
//   sceneColor   - Scene color buffer sampler
//   sceneDepth   - Scene depth buffer sampler
//   near, far    - Camera near/far planes
//   reverseZ     - Whether depth buffer uses reverse-Z
vec4 traceSSR(
    vec3 viewPos,
    vec3 viewNormal,
    mat4 projMatrix,
    sampler2D sceneColor,
    sampler2D sceneDepth,
    float near,
    float far,
    bool reverseZ
)
{
    // Calculate view direction (from camera to fragment)
    vec3 viewDir = normalize(viewPos);

    // Calculate reflection direction
    vec3 reflectDir = reflect(viewDir, viewNormal);

    // Reject rays pointing toward camera (would reflect sky/nothing)
    if (reflectDir.z > 0.0)
        return vec4(0.0, 0.0, 0.0, 0.0);

    // Starting position for raymarching
    vec3 rayPos = viewPos;

    // Adaptive step size - start small, increase with distance
    float stepSize = SSR_MAX_DISTANCE / float(SSR_MAX_STEPS);

    // Raymarching loop
    for (int i = 0; i < SSR_MAX_STEPS; ++i)
    {
        // Advance ray position
        rayPos += reflectDir * stepSize;

        // Project to screen space
        vec4 clipPos = projMatrix * vec4(rayPos, 1.0);
        vec3 ndcPos = clipPos.xyz / clipPos.w;

        // Convert to screen UV coordinates [0, 1]
        vec2 screenUV = ndcPos.xy * 0.5 + 0.5;

        // Check if outside screen bounds
        if (screenUV.x < 0.0 || screenUV.x > 1.0 ||
            screenUV.y < 0.0 || screenUV.y > 1.0)
        {
            return vec4(0.0, 0.0, 0.0, 0.0);
        }

        // Sample scene depth at this screen position
        float sampledDepth = texture2D(sceneDepth, screenUV).r;
        float linearSampledDepth = ssrLinearizeDepth(sampledDepth, near, far, reverseZ);

        // Calculate ray depth at this position
        float rayDepth = -rayPos.z; // View space Z is negative

        // Check for intersection
        float depthDiff = rayDepth - linearSampledDepth;

        // Hit detection: ray must be behind surface but within threshold
        if (depthDiff >= 0.0 && depthDiff < SSR_MAX_DIFF)
        {
            // Sample scene color at hit position
            vec3 hitColor = texture2D(sceneColor, screenUV).rgb;

            // Calculate confidence based on screen edge proximity
            float edgeFade = ssrScreenEdgeFade(screenUV);

            // Also fade based on ray travel distance
            float distanceFade = 1.0 - (float(i) / float(SSR_MAX_STEPS));

            // Combine fade factors
            float confidence = edgeFade * distanceFade;

            return vec4(hitColor, confidence);
        }

        // Adaptive step size: increase as we travel further
        stepSize *= 1.2;
    }

    // No hit found
    return vec4(0.0, 0.0, 0.0, 0.0);
}

// Simplified SSR trace that uses gl_ProjectionMatrix directly
// For use in fragment shaders where projection matrix is available
vec4 traceSSRSimple(
    vec3 viewPos,
    vec3 viewNormal,
    sampler2D sceneColor,
    sampler2D sceneDepth,
    float near,
    float far,
    bool reverseZ
)
{
    return traceSSR(viewPos, viewNormal, gl_ProjectionMatrix,
                    sceneColor, sceneDepth, near, far, reverseZ);
}

#endif // LIB_WATER_SSR
