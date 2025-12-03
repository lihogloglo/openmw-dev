#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION EVALUATION SHADER (Compatibility Profile)
// ============================================================================
// Generates new vertices from tessellated patches and applies displacement
// from the snow/terrain deformation RTT texture.
// Uses gl_ModelViewMatrix from compatibility profile.
// Uses custom projectionMatrix uniform for reverse-Z depth buffer support.
// ============================================================================

layout(triangles, equal_spacing, ccw) in;

// OpenMW uses a custom projection matrix for reverse-Z depth buffer
// Do NOT use gl_ProjectionMatrix - it doesn't contain the correct values
uniform mat4 projectionMatrix;

// Inputs from tessellation control shader
in TCS_OUT {
    vec3 position;      // Local chunk position
    vec3 worldPosition; // World position for deformation
    vec3 normal;
    vec4 color;
    vec2 texCoord;
    vec4 terrainWeights;
} tes_in[];

// Outputs to fragment shader
out TES_OUT {
    vec2 texCoord;
    vec3 normal;
    vec4 color;
    vec3 worldPos;
    float deformationFactor;
    float maxDepth;
    vec3 viewPos;
    float euclideanDepth;
    float linearDepth;
} tes_out;

// Deformation uniforms
uniform sampler2D snowDeformationMap;
uniform vec3 snowRTTWorldOrigin;
uniform float snowRTTScale;
uniform bool snowDeformationEnabled;

// Terrain-specific deformation depths
uniform float snowDeformationDepth;
uniform float ashDeformationDepth;
uniform float mudDeformationDepth;

// Depth calculation
uniform float linearFac;  // For linear depth calculation

// Interpolate using barycentric coordinates
vec3 interpolate3(vec3 v0, vec3 v1, vec3 v2)
{
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}

vec2 interpolate2(vec2 v0, vec2 v1, vec2 v2)
{
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}

vec4 interpolate4(vec4 v0, vec4 v1, vec4 v2)
{
    return gl_TessCoord.x * v0 + gl_TessCoord.y * v1 + gl_TessCoord.z * v2;
}

void main()
{
    // Interpolate LOCAL position (for transform), world position (for deformation)
    vec3 localPosition = interpolate3(tes_in[0].position, tes_in[1].position, tes_in[2].position);
    vec3 worldPosition = interpolate3(tes_in[0].worldPosition, tes_in[1].worldPosition, tes_in[2].worldPosition);
    vec3 normal = normalize(interpolate3(tes_in[0].normal, tes_in[1].normal, tes_in[2].normal));
    vec2 texCoord = interpolate2(tes_in[0].texCoord, tes_in[1].texCoord, tes_in[2].texCoord);
    vec4 color = interpolate4(tes_in[0].color, tes_in[1].color, tes_in[2].color);
    vec4 terrainWeights = interpolate4(tes_in[0].terrainWeights, tes_in[1].terrainWeights, tes_in[2].terrainWeights);

    // Initialize deformation outputs
    float deformationFactor = 0.0;
    float maxDepth = 0.0;
    float zOffset = 0.0;  // Deformation offset to apply to Z

    // Apply terrain deformation if enabled
    if (snowDeformationEnabled)
    {
        // Calculate terrain-specific lift based on weights
        float baseLift = terrainWeights.x * snowDeformationDepth +
                        terrainWeights.y * ashDeformationDepth +
                        terrainWeights.z * mudDeformationDepth;

        // Only process if this vertex is deformable (not pure rock)
        if (baseLift > 0.01)
        {
            // Use WORLD position for deformation UV calculation
            vec2 deformUV = (worldPosition.xy - snowRTTWorldOrigin.xy) / snowRTTScale + 0.5;

            // Sample the deformation map if within bounds
            if (deformUV.x >= 0.0 && deformUV.x <= 1.0 && deformUV.y >= 0.0 && deformUV.y <= 1.0)
            {
                deformationFactor = texture(snowDeformationMap, deformUV).r;
            }

            // Calculate deformation offset
            zOffset = baseLift * (1.0 - deformationFactor);
            maxDepth = baseLift;
        }
    }

    // Apply deformation to LOCAL position (same offset applies to both)
    localPosition.z += zOffset;
    worldPosition.z += zOffset;

    // Store world position for fragment shader (POM, etc.)
    tes_out.worldPos = worldPosition;

    // Transform LOCAL position to clip space
    // Use gl_ModelViewMatrix for view transform (compatibility profile)
    // Use custom projectionMatrix uniform for projection (reverse-Z depth buffer support)
    vec4 viewPos = gl_ModelViewMatrix * vec4(localPosition, 1.0);
    gl_Position = projectionMatrix * viewPos;

    // Calculate depths
    tes_out.euclideanDepth = length(viewPos.xyz);
    tes_out.linearDepth = gl_Position.z * linearFac;
    tes_out.viewPos = viewPos.xyz;

    // Pass through interpolated data
    tes_out.texCoord = texCoord;
    tes_out.normal = normal;
    tes_out.color = color;
    tes_out.deformationFactor = deformationFactor;
    tes_out.maxDepth = maxDepth;
}
