#version 400 core

// ============================================================================
// TERRAIN TESSELLATION VERTEX SHADER (Core Profile)
// ============================================================================
// This shader prepares vertex data for the tessellation stages.
// The actual displacement happens in the tessellation evaluation shader.
// ============================================================================

// Vertex inputs
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aTexCoord0;
layout(location = 6) in vec4 aTerrainWeights;  // x=snow, y=ash, z=mud, w=rock

// Outputs to tessellation control shader
out VS_OUT {
    vec3 position;
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} vs_out;

// Uniforms
uniform vec3 chunkWorldOffset;  // Chunk's world position (for local->world conversion)

void main()
{
    // Pass through vertex data to tessellation control shader
    // Convert to world space for tessellation calculations
    vs_out.position = aPosition + chunkWorldOffset;
    vs_out.normal = aNormal;
    vs_out.color = aColor;
    vs_out.texCoord = aTexCoord0;
    vs_out.terrainWeights = aTerrainWeights;
}
