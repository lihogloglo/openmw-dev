<<<<<<< HEAD
#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION VERTEX SHADER (Compatibility Profile)
// ============================================================================
// This shader prepares vertex data for the tessellation stages.
// Uses compatibility profile to access gl_Vertex, gl_Normal, etc.
// ============================================================================

// Outputs to tessellation control shader
out VS_OUT {
    vec3 position;      // Local chunk position (for transform by ModelView in TES)
    vec3 worldPosition; // World position (for tessellation LOD and deformation)
=======
#version 400 core

// ============================================================================
// TERRAIN TESSELLATION VERTEX SHADER (Core Profile)
// ============================================================================
// This shader prepares vertex data for the tessellation stages.
// The actual displacement happens in the tessellation evaluation shader.
// ============================================================================

// Vertex inputs - using OSG's default attribute binding locations
// OSG binds: Vertex=0, Normal=2, Color=3, TexCoord0=8
layout(location = 0) in vec3 osg_Vertex;
layout(location = 2) in vec3 osg_Normal;
layout(location = 3) in vec4 osg_Color;
layout(location = 8) in vec2 osg_MultiTexCoord0;
layout(location = 6) in vec4 aTerrainWeights;  // x=snow, y=ash, z=mud, w=rock (custom attribute)

// Outputs to tessellation control shader
out VS_OUT {
    vec3 position;
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} vs_out;

<<<<<<< HEAD
// Custom vertex attribute for terrain weights
attribute vec4 terrainWeights;  // x=snow, y=ash, z=mud, w=rock

=======
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771
// Uniforms
uniform vec3 chunkWorldOffset;  // Chunk's world position (for local->world conversion)

void main()
{
    // Pass through vertex data to tessellation control shader
<<<<<<< HEAD
    // Keep LOCAL position for transformation - model matrix handles placement
    vs_out.position = gl_Vertex.xyz;
    // Also compute world position for tessellation LOD calculation and deformation
    vs_out.worldPosition = gl_Vertex.xyz + chunkWorldOffset;
    vs_out.normal = gl_Normal;
    vs_out.color = gl_Color;
    vs_out.texCoord = gl_MultiTexCoord0.xy;
    vs_out.terrainWeights = terrainWeights;
=======
    // Convert to world space for tessellation calculations
    vs_out.position = osg_Vertex + chunkWorldOffset;
    vs_out.normal = osg_Normal;
    vs_out.color = osg_Color;
    vs_out.texCoord = osg_MultiTexCoord0;
    vs_out.terrainWeights = aTerrainWeights;
>>>>>>> 9be566fed3876deb9cfc699d014ce1d42856b771
}
