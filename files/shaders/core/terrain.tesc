#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION CONTROL SHADER (Compatibility Profile)
// ============================================================================
// Controls the tessellation level based on distance from camera.
// Outputs quad patch control points to the tessellation evaluation shader.
// ============================================================================

layout(vertices = 4) out;  // Quad patches with 4 control points

// Inputs from vertex shader
in VS_OUT {
    vec3 position;      // Local chunk position
    vec3 normal;
    vec4 color;
    vec2 texCoord;
} tcs_in[];

// Outputs to tessellation evaluation shader
out TCS_OUT {
    vec3 position;      // Local chunk position
    vec3 normal;
    vec4 color;
    vec2 texCoord;
} tcs_out[];

// Uniforms for LOD calculation
uniform float tessMinDistance;    // Distance at which max tessellation applies (default: 100)
uniform float tessMaxDistance;    // Distance at which min tessellation applies (default: 1000)
uniform float tessMinLevel;       // Minimum tessellation level (default: 1)
uniform float tessMaxLevel;       // Maximum tessellation level (default: 16)

// Calculate tessellation level based on XY distance (ignoring height difference)
// Uses smoothstep for gradual transitions and quantizes to prevent flickering
float calcTessLevel(vec3 localPos0, vec3 localPos1, vec3 localCameraPos)
{
    // Use edge midpoint for distance calculation
    vec3 edgeMidpoint = (localPos0 + localPos1) * 0.5;

    // Use XY-only distance to avoid tessellation changes when camera height changes
    // This prevents the issue where raising the camera reduces all tessellation
    vec2 xyDiff = edgeMidpoint.xy - localCameraPos.xy;
    float dist = length(xyDiff);

    // Use smoothstep for gradual transition instead of linear interpolation
    // This adds natural hysteresis and smoother LOD transitions
    float t = smoothstep(tessMinDistance, tessMaxDistance, dist);

    // Calculate tessellation level (higher when closer)
    float level = mix(tessMaxLevel, tessMinLevel, t);

    // Quantize to nearest 0.5 to prevent frame-to-frame flickering
    // GPU tessellation has discrete levels, so unquantized values cause jitter
    return floor(level * 2.0 + 0.5) / 2.0;
}

void main()
{
    // Pass through control point data
    tcs_out[gl_InvocationID].position = tcs_in[gl_InvocationID].position;
    tcs_out[gl_InvocationID].normal = tcs_in[gl_InvocationID].normal;
    tcs_out[gl_InvocationID].color = tcs_in[gl_InvocationID].color;
    tcs_out[gl_InvocationID].texCoord = tcs_in[gl_InvocationID].texCoord;

    // Only the first invocation sets tessellation levels
    if (gl_InvocationID == 0)
    {
        // Get camera position in local chunk space by inverting the model-view matrix
        // This ensures correct distance calculation regardless of chunk world position
        vec3 localCameraPos = (gl_ModelViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

        // Quad vertices are ordered: 0=bottom-left, 1=bottom-right, 2=top-right, 3=top-left
        vec3 p0 = tcs_in[0].position;
        vec3 p1 = tcs_in[1].position;
        vec3 p2 = tcs_in[2].position;
        vec3 p3 = tcs_in[3].position;

        // Calculate patch center for a unified tessellation level
        // Using patch center instead of per-edge midpoints reduces anisotropic artifacts
        vec3 patchCenter = (p0 + p1 + p2 + p3) * 0.25;

        // Calculate base tessellation level from patch center
        float baseTessLevel = calcTessLevel(patchCenter, patchCenter, localCameraPos);

        // Calculate per-edge tessellation levels for proper neighbor matching
        // Each edge uses its own level to ensure T-junction-free tessellation with neighbors
        float tessLeft   = calcTessLevel(p0, p3, localCameraPos);  // left edge (0-3)
        float tessBottom = calcTessLevel(p0, p1, localCameraPos);  // bottom edge (0-1)
        float tessRight  = calcTessLevel(p1, p2, localCameraPos);  // right edge (1-2)
        float tessTop    = calcTessLevel(p3, p2, localCameraPos);  // top edge (3-2)

        // Set outer tessellation levels for each edge
        gl_TessLevelOuter[0] = tessLeft;
        gl_TessLevelOuter[1] = tessBottom;
        gl_TessLevelOuter[2] = tessRight;
        gl_TessLevelOuter[3] = tessTop;

        // Inner tessellation uses average of opposite edges for balanced subdivision
        // This creates more uniform interior tessellation
        gl_TessLevelInner[0] = (tessBottom + tessTop) * 0.5;    // horizontal
        gl_TessLevelInner[1] = (tessLeft + tessRight) * 0.5;    // vertical
    }
}
