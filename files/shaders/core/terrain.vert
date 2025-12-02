#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION VERTEX SHADER (Compatibility Profile)
// ============================================================================
// This shader prepares vertex data for the tessellation stages.
// Uses compatibility profile to access gl_Vertex, gl_Normal, etc.
// ============================================================================

// Outputs to tessellation control shader
out VS_OUT {
    vec3 position;      // Local chunk position (for ModelView transform in TES and LOD calculation in TCS)
    vec3 worldPosition; // World position (for deformation UV lookup only - NOT for LOD!)
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} vs_out;

// Custom vertex attribute for terrain weights
attribute vec4 terrainWeights;  // x=snow, y=ash, z=mud, w=rock

// Uniforms
uniform vec3 chunkWorldOffset;  // Chunk's world position (for local->world conversion)

void main()
{
    // Pass through vertex data to tessellation control shader
    // Keep LOCAL position for transformation - model matrix handles placement
    vs_out.position = gl_Vertex.xyz;
    // Also compute world position for tessellation LOD calculation and deformation
    vs_out.worldPosition = gl_Vertex.xyz + chunkWorldOffset;
    vs_out.normal = gl_Normal;
    vs_out.color = gl_Color;
    vs_out.texCoord = gl_MultiTexCoord0.xy;
    vs_out.terrainWeights = terrainWeights;
}
