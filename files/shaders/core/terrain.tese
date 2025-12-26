#version 400 compatibility

// ============================================================================
// TERRAIN TESSELLATION EVALUATION SHADER (Compatibility Profile)
// ============================================================================
// Generates new vertices from tessellated quad patches and applies
// heightmap displacement from normal map alpha channel.
// Uses gl_ModelViewMatrix from compatibility profile.
// Uses custom projectionMatrix uniform for reverse-Z depth buffer support.
// ============================================================================

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

layout(quads, equal_spacing, ccw) in;

// OpenMW uses a custom projection matrix for reverse-Z depth buffer
uniform mat4 projectionMatrix;

// Include shadow coordinate setup
#include "compatibility/shadows_vertex.glsl"

// Inputs from tessellation control shader
in TCS_OUT {
    vec3 position;      // Local chunk position
    vec3 normal;
    vec4 color;
    vec2 texCoord;
} tes_in[];

// Outputs to fragment shader
out TES_OUT {
    vec2 texCoord;
    vec3 normal;
    vec4 color;
    vec3 viewPos;
    float euclideanDepth;
    float linearDepth;
} tes_out;

// Heightmap displacement uniforms
uniform sampler2D displacementMap;  // Blended displacement map (RG: weighted height, weight)
uniform bool heightmapDisplacementEnabled;
uniform float heightmapDisplacementStrength;

// Tessellation distance uniforms (used for displacement falloff)
uniform float tessMinDistance;
uniform float tessMaxDistance;

// Depth calculation
uniform float linearFac;

// Bilinear interpolation for quads using gl_TessCoord.xy
// Quad vertices are ordered: 0=bottom-left, 1=bottom-right, 2=top-right, 3=top-left
vec3 interpolate3(vec3 v0, vec3 v1, vec3 v2, vec3 v3)
{
    vec3 bottom = mix(v0, v1, gl_TessCoord.x);
    vec3 top = mix(v3, v2, gl_TessCoord.x);
    return mix(bottom, top, gl_TessCoord.y);
}

vec2 interpolate2(vec2 v0, vec2 v1, vec2 v2, vec2 v3)
{
    vec2 bottom = mix(v0, v1, gl_TessCoord.x);
    vec2 top = mix(v3, v2, gl_TessCoord.x);
    return mix(bottom, top, gl_TessCoord.y);
}

vec4 interpolate4(vec4 v0, vec4 v1, vec4 v2, vec4 v3)
{
    vec4 bottom = mix(v0, v1, gl_TessCoord.x);
    vec4 top = mix(v3, v2, gl_TessCoord.x);
    return mix(bottom, top, gl_TessCoord.y);
}

void main()
{
    // Interpolate vertex attributes
    vec3 position = interpolate3(tes_in[0].position, tes_in[1].position, tes_in[2].position, tes_in[3].position);
    vec3 normal = normalize(interpolate3(tes_in[0].normal, tes_in[1].normal, tes_in[2].normal, tes_in[3].normal));
    vec2 texCoord = interpolate2(tes_in[0].texCoord, tes_in[1].texCoord, tes_in[2].texCoord, tes_in[3].texCoord);
    vec4 color = interpolate4(tes_in[0].color, tes_in[1].color, tes_in[2].color, tes_in[3].color);

    // Apply heightmap displacement from blended displacement map
    // The displacement map is pre-rendered with world-space UV offsets, ensuring
    // seamless displacement across chunk boundaries.
    if (heightmapDisplacementEnabled)
    {
        // Sample from the pre-computed blended displacement map
        // The displacement map uses chunk UV coordinates (0-1)
        // RG format: R = weighted height sum, G = weight sum
        vec2 dispSample = texture(displacementMap, texCoord).rg;

        // Compute final blended height: weighted average
        // If weight is zero, default to neutral height 0.5
        float height = (dispSample.g > 0.001) ? (dispSample.r / dispSample.g) : 0.5;

        // Displace along the normal direction
        // Height of 0.5 is neutral (no displacement), 0 is down, 1 is up
        float displacement = (height - 0.5) * heightmapDisplacementStrength;

        // Calculate distance-based falloff using XY distance only
        // This matches the TCS shader's distance calculation for consistency
        vec3 localCameraPos = (gl_ModelViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
        vec2 xyDiff = position.xy - localCameraPos.xy;
        float distToCamera = length(xyDiff);

        // Smoothly fade out displacement between tessellation min and max distance
        float falloff = 1.0 - smoothstep(tessMinDistance, tessMaxDistance, distToCamera);
        displacement *= falloff;

        // Fade out displacement near chunk edges to hide seams between chunks
        // Each chunk has its own displacement map RTT, so edge values may differ slightly
        // Using a narrow fade (2%) minimizes visible loss while eliminating seams
        float edgeMargin = 0.02;
        float edgeFadeX = smoothstep(0.0, edgeMargin, texCoord.x) * smoothstep(0.0, edgeMargin, 1.0 - texCoord.x);
        float edgeFadeY = smoothstep(0.0, edgeMargin, texCoord.y) * smoothstep(0.0, edgeMargin, 1.0 - texCoord.y);
        displacement *= edgeFadeX * edgeFadeY;

        // Apply displacement along the vertex normal
        position += normal * displacement;
    }

    // Transform position to clip space
    vec4 viewPos = gl_ModelViewMatrix * vec4(position, 1.0);
    gl_Position = projectionMatrix * viewPos;

    // Calculate depths
    tes_out.euclideanDepth = length(viewPos.xyz);
    tes_out.linearDepth = gl_Position.z * linearFac;
    tes_out.viewPos = viewPos.xyz;

    // Pass through interpolated data
    tes_out.texCoord = texCoord;
    tes_out.normal = normal;
    tes_out.color = color;

    // Setup shadow coordinates
    vec3 viewNormal = normalize(gl_NormalMatrix * normal);
    setupShadowCoords(viewPos, viewNormal);
}
