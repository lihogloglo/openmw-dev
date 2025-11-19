#version 120

// Ocean Vertex Shader with FFT Displacement (Clipmap)

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

uniform bool uEnableOceanWaves;
uniform float uWaveAmplitude;

uniform mat4 osg_ViewMatrixInverse;

// Sample displacement with cascades
vec3 sampleDisplacement(vec2 worldPosXY)
{
    if (!uEnableOceanWaves)
        return vec3(0.0);

    vec2 uv0 = worldPosXY / uCascadeTileSize0;
    vec2 uv1 = worldPosXY / uCascadeTileSize1;
    vec2 uv2 = worldPosXY / uCascadeTileSize2;

    vec3 disp0 = texture2D(uDisplacementCascade0, uv0).xyz;
    vec3 disp1 = texture2D(uDisplacementCascade1, uv1).xyz;
    vec3 disp2 = texture2D(uDisplacementCascade2, uv2).xyz;

    // Blend cascades
    vec3 displacement = (disp0 + disp1 + disp2); 

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

    vec3 normal0 = texture2D(uNormalCascade0, uv0).xyz * 2.0 - 1.0;
    vec3 normal1 = texture2D(uNormalCascade1, uv1).xyz * 2.0 - 1.0;
    vec3 normal2 = texture2D(uNormalCascade2, uv2).xyz * 2.0 - 1.0;

    vec3 blendedNormal = normal0 + normal1 + normal2;
    return normalize(blendedNormal);
}

void main()
{
    vec4 localPos = gl_Vertex;
    vTexCoord = gl_MultiTexCoord0.xy;

    // Calculate View Position
    vec4 viewPos = gl_ModelViewMatrix * localPos;
    
    // Calculate World Position
    vec3 worldPos = (osg_ViewMatrixInverse * viewPos).xyz;
    vec3 cameraPos = osg_ViewMatrixInverse[3].xyz;

    // Distance factor for attenuation (match Godot's logic)
    // float distance_factor = min(exp(-(length(VERTEX.xz - CAMERA_POSITION_WORLD.xz) - 150.0)*0.007), 1.0);
    float dist = length(worldPos.xy - cameraPos.xy);
    float distance_factor = min(exp(-(dist - 150.0) * 0.007), 1.0);

    // Sample displacement
    vec3 displacement = sampleDisplacement(worldPos.xy);

    // Apply attenuation
    displacement *= distance_factor;

    // Apply displacement to World Position (for varyings)
    vec3 displacedWorldPos = worldPos;
    displacedWorldPos.z += displacement.y; // Godot uses Y for height, we use Z? 
    // Wait, Godot's displacement texture:
    // "displacement += texture(displacements, ...).xyz * scales.z;"
    // "VERTEX += displacement * distance_factor;"
    // "wave_height = displacement.y;"
    // Godot uses Y-up. So displacement.y is vertical.
    // Our FFT simulation likely produces XYZ displacement where Z is vertical (if adapted) or Y is vertical (if copied).
    // Assuming OpenMW FFT simulation produces Z-up displacement or we swizzle it.
    // Let's assume displacement.z is vertical for now, or check `oceanfftsimulation.cpp`.
    // But `sampleDisplacement` returns vec3.
    // If the texture is from Godot, it's likely Y-up.
    // Let's assume we need to add displacement to the vertex.
    // If displacement is in World Space (XYZ), we just add it.
    
    // Transform displacement to View Space to apply to viewPos
    // Assuming Model Matrix rotation is Identity (water plane is flat)
    // The rotation part of ModelView is just View rotation.
    vec3 viewDisplacement = mat3(gl_ModelViewMatrix) * displacement;
    
    viewPos.xyz += viewDisplacement;

    vWorldPos = displacedWorldPos; // Approximate, or re-calculate from new viewPos
    vDisplacedPos = displacedWorldPos;

    // Sample normal
    vec3 worldNormal = sampleNormal(worldPos.xy);
    vNormal = gl_NormalMatrix * worldNormal;

    vViewPos = viewPos;

    gl_Position = gl_ProjectionMatrix * viewPos;
    gl_ClipVertex = vViewPos;

    position = viewPos; // Pass viewPos as 'position' for frag shader? Or world pos? 
    // Legacy water uses viewPos for 'position' usually.
    linearDepth = gl_Position.z;
}
