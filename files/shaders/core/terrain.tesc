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
uniform vec3 cameraPos;           // Camera position in local/model space
uniform float tessMinDistance;    // Distance at which max tessellation applies (default: 100)
uniform float tessMaxDistance;    // Distance at which min tessellation applies (default: 1000)
uniform float tessMinLevel;       // Minimum tessellation level (default: 1)
uniform float tessMaxLevel;       // Maximum tessellation level (default: 16)

// Calculate tessellation level based on distance
// NOTE: cameraPos is in local/model space (from CullVisitor::getEyePoint())
float calcTessLevel(vec3 localPos0, vec3 localPos1)
{
    // Use edge midpoint for distance calculation
    vec3 edgeMidpoint = (localPos0 + localPos1) * 0.5;
    float dist = length(edgeMidpoint - cameraPos);

    // Linear interpolation between min and max tessellation based on distance
    float t = clamp((dist - tessMinDistance) / (tessMaxDistance - tessMinDistance), 0.0, 1.0);

    // Higher tessellation when closer (invert t)
    return mix(tessMaxLevel, tessMinLevel, t);
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
        // Quad vertices are ordered: 0=bottom-left, 1=bottom-right, 2=top-right, 3=top-left
        vec3 p0 = tcs_in[0].position;
        vec3 p1 = tcs_in[1].position;
        vec3 p2 = tcs_in[2].position;
        vec3 p3 = tcs_in[3].position;

        // Calculate tessellation level for each edge
        // For quads: outer[0]=left, outer[1]=bottom, outer[2]=right, outer[3]=top
        gl_TessLevelOuter[0] = calcTessLevel(p0, p3);  // left edge (0-3)
        gl_TessLevelOuter[1] = calcTessLevel(p0, p1);  // bottom edge (0-1)
        gl_TessLevelOuter[2] = calcTessLevel(p1, p2);  // right edge (1-2)
        gl_TessLevelOuter[3] = calcTessLevel(p3, p2);  // top edge (3-2)

        // Inner tessellation levels for horizontal and vertical subdivision
        gl_TessLevelInner[0] = (gl_TessLevelOuter[1] + gl_TessLevelOuter[3]) * 0.5;  // horizontal
        gl_TessLevelInner[1] = (gl_TessLevelOuter[0] + gl_TessLevelOuter[2]) * 0.5;  // vertical
    }
}
