#version 120

// Ocean Vertex Shader with FFT Displacement
// Applies wave displacement from FFT simulation

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#include "lib/core/vertex.h.glsl"

varying vec3 vWorldPos;
varying vec3 vDisplacedPos;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vViewPos;

// Required by water.frag-based system
varying vec4 position;
varying float linearDepth;

// Displacement textures from FFT cascades
uniform sampler2D uDisplacementCascade0;
uniform sampler2D uDisplacementCascade1;
uniform sampler2D uDisplacementCascade2;

// Normal textures from FFT cascades
uniform sampler2D uNormalCascade0;
uniform sampler2D uNormalCascade1;
uniform sampler2D uNormalCascade2;

// Cascade parameters
uniform float uCascadeTileSize0;
uniform float uCascadeTileSize1;
uniform float uCascadeTileSize2;

uniform bool uEnableOceanWaves;  // Enable/disable FFT waves
uniform float uWaveAmplitude;     // Global wave amplitude multiplier

// Sample displacement with cascades
vec3 sampleDisplacement(vec2 worldPosXY)
{
    if (!uEnableOceanWaves)
        return vec3(0.0);

    // Sample from multiple cascades and blend
    vec2 uv0 = worldPosXY / uCascadeTileSize0;
    vec2 uv1 = worldPosXY / uCascadeTileSize1;
    vec2 uv2 = worldPosXY / uCascadeTileSize2;

    vec3 disp0 = texture2D(uDisplacementCascade0, uv0).xyz;
    vec3 disp1 = texture2D(uDisplacementCascade1, uv1).xyz;
    vec3 disp2 = texture2D(uDisplacementCascade2, uv2).xyz;

    // Blend cascades (can be improved with better weighting)
    vec3 displacement = (disp0 + disp1 * 0.5 + disp2 * 0.25) / 1.75;

    return displacement * uWaveAmplitude;
}

// Sample normals with cascades
vec3 sampleNormal(vec2 worldPosXY)
{
    if (!uEnableOceanWaves)
        return vec3(0.0, 0.0, 1.0);

    vec2 uv0 = worldPosXY / uCascadeTileSize0;
    vec2 uv1 = worldPosXY / uCascadeTileSize1;
    vec2 uv2 = worldPosXY / uCascadeTileSize2;

    // Normals are encoded as [0,1], decode to [-1,1]
    vec3 normal0 = texture2D(uNormalCascade0, uv0).xyz * 2.0 - 1.0;
    vec3 normal1 = texture2D(uNormalCascade1, uv1).xyz * 2.0 - 1.0;
    vec3 normal2 = texture2D(uNormalCascade2, uv2).xyz * 2.0 - 1.0;

    // Blend cascades with proper weighting
    vec3 blendedNormal = normal0 + normal1 * 0.5 + normal2 * 0.25;

    // Normalize after blending
    return normalize(blendedNormal);
}

void main()
{
    // gl_Vertex is in LOCAL chunk space (0 to CHUNK_SIZE)
    // The PositionAttitudeTransform node positions the chunk in world space
    vec4 localPos = gl_Vertex;
    vTexCoord = gl_MultiTexCoord0.xy;

    // Calculate world position
    // We need the world XY for FFT texture sampling, but ModelViewMatrix includes the view transform
    // For now, use the model transform to get world position
    // TODO: Pass world position more explicitly via uniform or vertex attribute
    vec4 modelPos = gl_ModelViewMatrix * localPos;

    // Sample FFT displacement using world-space XY coordinates
    // The transform node positions chunks, so we approximate world XY from model space
    vec2 worldPosXY = modelPos.xy;
    vWorldPos = vec3(worldPosXY, localPos.z);

    // Sample displacement and apply it
    vec3 displacement = sampleDisplacement(worldPosXY);
    vec3 displacedPos = localPos.xyz + displacement;
    vDisplacedPos = displacedPos;

    // Sample normal from FFT (use world position for sampling)
    vec3 worldNormal = sampleNormal(worldPosXY);
    vNormal = gl_NormalMatrix * worldNormal;

    // Transform displaced position
    vec4 displacedVertex = vec4(displacedPos, 1.0);
    vViewPos = modelToView(displacedVertex);

    // Final position
    gl_Position = modelToClip(displacedVertex);
    gl_ClipVertex = vViewPos;

    // Required outputs for water system integration
    position = displacedVertex;
    linearDepth = gl_Position.z;
}
