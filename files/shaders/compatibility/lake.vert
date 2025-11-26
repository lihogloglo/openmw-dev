#version 120

// Lake vertex shader with proper world-space calculations for SSR/cubemap
// Fixes reflection jumping by using consistent world-space coordinates

varying vec2 vTexCoord;
varying vec3 vWorldPos;     // True world position (not view space!)
varying vec3 vViewPos;      // Position in view/camera space
varying vec4 vScreenPos;    // Clip space position for SSR sampling
varying float vLinearDepth; // Linear depth for fog/effects

// Matrices provided by LakeStateSetUpdater
uniform mat4 viewMatrix;
uniform mat4 projMatrix;
uniform mat4 invViewMatrix;
uniform vec3 cameraPos;

// Near/far for depth calculations
uniform float near;
uniform float far;

void main()
{
    vTexCoord = gl_MultiTexCoord0.xy;

    // Get world position from model matrix
    // gl_ModelViewMatrix = viewMatrix * modelMatrix
    // We need to extract the model matrix part
    // Since lake cells are positioned via PositionAttitudeTransform, gl_Vertex is in local space
    // and gl_ModelViewMatrix transforms to view space
    //
    // To get world position: we use the fact that our lake geometry is simple:
    // The PositionAttitudeTransform positions the lake at (cellCenterX, cellCenterY, height)
    // and the geometry vertices are in local space around origin
    //
    // We can compute world position by: worldPos = invViewMatrix * viewPos
    vec4 viewPos4 = gl_ModelViewMatrix * gl_Vertex;
    vViewPos = viewPos4.xyz;

    // Transform view position back to world space for stable reflections
    // This is the key fix for reflection jumping!
    vec4 worldPos4 = invViewMatrix * viewPos4;
    vWorldPos = worldPos4.xyz;

    // Clip space position for standard rendering
    gl_Position = gl_ProjectionMatrix * viewPos4;

    // Store screen position for SSR texture sampling
    vScreenPos = gl_Position;

    // Linear depth for fog calculations
    // Use the actual distance from camera, not clip Z
    vLinearDepth = -vViewPos.z;
}
