#version 120

// Ocean Vertex Shader with FFT Displacement
// Applies wave displacement from FFT simulation

#if @useUBO
    #define UNIFORM(NAME) NAME
#else
    #define UNIFORM(NAME) gl_##NAME
#endif

attribute vec3 aPosition;
attribute vec2 aTexCoord;
attribute vec3 aNormal;

varying vec3 vWorldPos;
varying vec3 vDisplacedPos;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vViewPos;

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

// Transform matrices
uniform mat4 uModelViewMatrix;
uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

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

    // Blend and normalize
    vec3 blendedNormal = normalize(normal0 + normal1 * 0.5 + normal2 * 0.25);

    return blendedNormal;
}

void main()
{
    // World position
    vWorldPos = aPosition;
    vTexCoord = aTexCoord;

    // Sample displacement from FFT
    vec3 displacement = sampleDisplacement(aPosition.xy);

    // Apply displacement
    vec3 displacedPos = aPosition + displacement;
    vDisplacedPos = displacedPos;

    // Sample normal from FFT
    vec3 displacedNormal = sampleNormal(aPosition.xy);
    vNormal = displacedNormal;

    // Transform to view space
    vViewPos = uModelViewMatrix * vec4(displacedPos, 1.0);

    // Final position
    gl_Position = uProjectionMatrix * vViewPos;
}
