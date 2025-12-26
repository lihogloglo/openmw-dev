#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION VERTEX SHADER (Compatibility Profile)
// ============================================================================
// Prepares vertex data for the tessellation stages.
// Uses compatibility profile to access gl_Vertex, gl_Normal, etc.
// ============================================================================

// Outputs to tessellation control shader
out VS_OUT {
    vec3 position;      // Local chunk position (for ModelView transform and LOD calculation)
    vec3 normal;
    vec4 color;
    vec2 texCoord;
} vs_out;

void main()
{
    // Pass through vertex data to tessellation control shader
    vs_out.position = gl_Vertex.xyz;
    vs_out.normal = gl_Normal;
    vs_out.color = gl_Color;
    vs_out.texCoord = gl_MultiTexCoord0.xy;
}
