#version 120

// Lake vertex shader - Adapted from ogwatershader/water.vert

varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 rippleMapUV;

// Uniforms
uniform mat4 viewMatrix;
uniform mat4 projMatrix;
uniform mat4 invViewMatrix;
uniform vec3 cameraPos;
uniform vec3 playerPos; // For ripple UVs

// Constants from water.vert
const float rippleMapSize = 512.0;
const float rippleMapWorldScale = 2.0;

// Helper function from lib/view/depth.glsl (inlined for simplicity if include fails, but we'll try include)
#include "lib/view/depth.glsl" 

// float getLinearDepth(float z_b, float view_z)
// {
//     // Simplified linear depth
//     return -view_z;
// }

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    
    position = gl_Vertex;
    
    // World pos calculation
    // gl_Vertex is in local space. For lakes, local space might be world space if transform is identity?
    // Lake cells are positioned by PositionAttitudeTransform.
    // So gl_Vertex is local to the cell center.
    // We need world position.
    // gl_ModelViewMatrix * gl_Vertex gives View Space.
    // invViewMatrix * ViewSpace gives World Space.
    
    vec4 viewPos4 = gl_ModelViewMatrix * gl_Vertex;
    vec3 viewPos = viewPos4.xyz;
    
    vec4 worldPos4 = invViewMatrix * viewPos4;
    worldPos = worldPos4.xyz / worldPos4.w;
    
    // Ripple UVs
    rippleMapUV = (worldPos.xy - playerPos.xy + (rippleMapSize * rippleMapWorldScale / 2.0)) / rippleMapSize / rippleMapWorldScale;
    
    linearDepth = -viewPos.z; // Standard linear depth approximation
}
