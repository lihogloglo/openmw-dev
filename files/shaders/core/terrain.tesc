<<<<<<< HEAD
#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION CONTROL SHADER (Compatibility Profile)
=======
#version 400 core

// ============================================================================
// TERRAIN TESSELLATION CONTROL SHADER
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771
// ============================================================================
// Controls the tessellation level based on distance from camera.
// Outputs patch control points to the tessellation evaluation shader.
// ============================================================================

layout(vertices = 3) out;  // Triangle patches with 3 control points

// Inputs from vertex shader
in VS_OUT {
<<<<<<< HEAD
    vec3 position;      // Local chunk position
    vec3 worldPosition; // World position for LOD/deformation
=======
    vec3 position;
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} tcs_in[];

// Outputs to tessellation evaluation shader
out TCS_OUT {
<<<<<<< HEAD
    vec3 position;      // Local chunk position
    vec3 worldPosition; // World position for deformation
=======
    vec3 position;
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} tcs_out[];

// Patch output for tessellation level (shared by all vertices in patch)
patch out float tessLevelDebug;

// Uniforms for LOD calculation
uniform vec3 cameraPos;           // Camera position in world space
uniform float tessMinDistance;    // Distance at which max tessellation applies (default: 100)
uniform float tessMaxDistance;    // Distance at which min tessellation applies (default: 1000)
uniform float tessMinLevel;       // Minimum tessellation level (default: 1)
uniform float tessMaxLevel;       // Maximum tessellation level (default: 16)

<<<<<<< HEAD
// Calculate tessellation level based on distance using WORLD positions
float calcTessLevel(vec3 worldPos0, vec3 worldPos1)
{
    // Use edge midpoint for distance calculation
    vec3 edgeMidpoint = (worldPos0 + worldPos1) * 0.5;
    float dist = length(edgeMidpoint - cameraPos);

    // Linear interpolation between min and max tessellation based on distance
    float t = clamp((dist - tessMinDistance) / (tessMaxDistance - tessMinDistance), 0.0, 1.0);
=======
// Calculate tessellation level based on distance
float calcTessLevel(vec3 pos0, vec3 pos1)
{
    // Use edge midpoint for distance calculation
    vec3 edgeMidpoint = (pos0 + pos1) * 0.5;
    float distance = length(edgeMidpoint - cameraPos);

    // Linear interpolation between min and max tessellation based on distance
    float t = clamp((distance - tessMinDistance) / (tessMaxDistance - tessMinDistance), 0.0, 1.0);
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771

    // Higher tessellation when closer (invert t)
    return mix(tessMaxLevel, tessMinLevel, t);
}

void main()
{
    // Pass through control point data
    tcs_out[gl_InvocationID].position = tcs_in[gl_InvocationID].position;
<<<<<<< HEAD
    tcs_out[gl_InvocationID].worldPosition = tcs_in[gl_InvocationID].worldPosition;
=======
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771
    tcs_out[gl_InvocationID].normal = tcs_in[gl_InvocationID].normal;
    tcs_out[gl_InvocationID].color = tcs_in[gl_InvocationID].color;
    tcs_out[gl_InvocationID].texCoord = tcs_in[gl_InvocationID].texCoord;
    tcs_out[gl_InvocationID].terrainWeights = tcs_in[gl_InvocationID].terrainWeights;

    // Only the first invocation sets tessellation levels
    if (gl_InvocationID == 0)
    {
<<<<<<< HEAD
        // Use WORLD positions for LOD calculation (cameraPos is in world space)
        vec3 p0 = tcs_in[0].worldPosition;
        vec3 p1 = tcs_in[1].worldPosition;
        vec3 p2 = tcs_in[2].worldPosition;
=======
        vec3 p0 = tcs_in[0].position;
        vec3 p1 = tcs_in[1].position;
        vec3 p2 = tcs_in[2].position;
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771

        // Calculate tessellation level for each edge
        // Edge 0: vertices 1-2, Edge 1: vertices 2-0, Edge 2: vertices 0-1
        gl_TessLevelOuter[0] = calcTessLevel(p1, p2);
        gl_TessLevelOuter[1] = calcTessLevel(p2, p0);
        gl_TessLevelOuter[2] = calcTessLevel(p0, p1);

        // Inner tessellation level is the average of outer levels
        gl_TessLevelInner[0] = (gl_TessLevelOuter[0] + gl_TessLevelOuter[1] + gl_TessLevelOuter[2]) / 3.0;

        // Pass tessellation level for debug visualization
        tessLevelDebug = gl_TessLevelInner[0];
    }
}
