#version 120

/**
 * Screen-Space Reflections (SSR) - Raymarch Fragment Shader
 *
 * Performs efficient screen-space raymarching for water reflections.
 * Outputs reflection color (RGB) and confidence (A).
 */

// Input textures
uniform sampler2D colorBuffer;
uniform sampler2D depthBuffer;
uniform sampler2D normalBuffer; // Optional, can use geometric normals

// Matrices
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 invViewProjection;

// SSR Parameters
uniform float maxDistance;
uniform int maxSteps;
uniform float stepSize;
uniform float thickness;
uniform vec2 fadeParams; // (fadeStart, fadeEnd) in normalized screen space

varying vec2 vTexCoord;

// Constants
const float NEAR_PLANE = 0.1;
const float FAR_PLANE = 10000.0;

/**
 * Linearize depth buffer value
 */
float linearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; // Back to NDC
    return (2.0 * NEAR_PLANE * FAR_PLANE) / (FAR_PLANE + NEAR_PLANE - z * (FAR_PLANE - NEAR_PLANE));
}

/**
 * Reconstruct world position from depth
 */
vec3 getWorldPosition(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
}

/**
 * Project world position to screen space
 */
vec3 projectToScreen(vec3 worldPos)
{
    vec4 clipPos = projectionMatrix * (viewMatrix * vec4(worldPos, 1.0));
    vec3 ndcPos = clipPos.xyz / clipPos.w;
    return vec3(ndcPos.xy * 0.5 + 0.5, ndcPos.z * 0.5 + 0.5);
}

/**
 * Calculate screen-edge fade factor
 */
float screenEdgeFade(vec2 uv)
{
    vec2 distToEdge = min(uv, 1.0 - uv);
    float minDist = min(distToEdge.x, distToEdge.y);
    return smoothstep(0.0, fadeParams.x - fadeParams.y, minDist - fadeParams.y);
}

/**
 * Main SSR raymarch function
 */
vec4 traceScreenSpaceRay(vec3 rayOrigin, vec3 rayDir, float maxDist, int steps)
{
    vec3 rayStep = rayDir * (maxDist / float(steps));
    vec3 currentPos = rayOrigin;

    for (int i = 0; i < steps; ++i)
    {
        currentPos += rayStep;

        // Project to screen space
        vec3 screenPos = projectToScreen(currentPos);

        // Check if outside screen bounds
        if (screenPos.x < 0.0 || screenPos.x > 1.0 ||
            screenPos.y < 0.0 || screenPos.y > 1.0)
        {
            return vec4(0.0, 0.0, 0.0, 0.0); // Miss
        }

        // Sample depth buffer at current screen position
        float sampledDepth = texture2D(depthBuffer, screenPos.xy).r;
        float sampledLinearDepth = linearizeDepth(sampledDepth);
        float rayLinearDepth = linearizeDepth(screenPos.z);

        // Check for intersection (ray passed through surface)
        float depthDiff = rayLinearDepth - sampledLinearDepth;
        if (depthDiff > 0.0 && depthDiff < thickness)
        {
            // Hit! Sample color buffer
            vec3 color = texture2D(colorBuffer, screenPos.xy).rgb;

            // Calculate confidence based on:
            // 1. Screen edge proximity
            // 2. Ray length (fade with distance)
            // 3. Intersection quality

            float edgeFade = screenEdgeFade(screenPos.xy);
            float distanceFade = 1.0 - (float(i) / float(steps));
            float intersectionQuality = 1.0 - clamp(depthDiff / thickness, 0.0, 1.0);

            float confidence = edgeFade * distanceFade * intersectionQuality;

            return vec4(color, confidence);
        }
    }

    // No hit found
    return vec4(0.0, 0.0, 0.0, 0.0);
}

void main()
{
    vec2 uv = vTexCoord;

    // Sample depth at water surface
    float depth = texture2D(depthBuffer, uv).r;

    // Early exit if at far plane (no geometry)
    if (depth >= 1.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Reconstruct world position
    vec3 worldPos = getWorldPosition(uv, depth);

    // Get camera position (inverse of view matrix translation)
    vec3 cameraPos = vec3(
        -(viewMatrix[3][0] * viewMatrix[0][0] + viewMatrix[3][1] * viewMatrix[0][1] + viewMatrix[3][2] * viewMatrix[0][2]),
        -(viewMatrix[3][0] * viewMatrix[1][0] + viewMatrix[3][1] * viewMatrix[1][1] + viewMatrix[3][2] * viewMatrix[1][2]),
        -(viewMatrix[3][0] * viewMatrix[2][0] + viewMatrix[3][1] * viewMatrix[2][1] + viewMatrix[3][2] * viewMatrix[2][2])
    );

    // Calculate view direction
    vec3 viewDir = normalize(worldPos - cameraPos);

    // Use geometric normal (water surface is flat, pointing up)
    // In a more advanced implementation, sample normalBuffer for per-pixel normals
    vec3 normal = vec3(0.0, 0.0, 1.0);

    // Calculate reflection direction
    vec3 reflectDir = reflect(viewDir, normal);

    // Trace reflection ray
    vec4 ssrResult = traceScreenSpaceRay(worldPos, reflectDir, maxDistance, int(maxSteps));

    // Output: RGB = reflection color, A = confidence
    gl_FragColor = ssrResult;
}
