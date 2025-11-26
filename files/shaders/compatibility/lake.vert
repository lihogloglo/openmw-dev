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

    // CRITICAL FIX: Use OSG's built-in vertex transformation to get stable world position
    // OSG provides gl_Vertex in local/model space, and gl_ModelViewMatrix transforms it
    //
    // The lake geometry is positioned by PositionAttitudeTransform which sets the model matrix
    // to place the water plane at (cellCenterX, cellCenterY, height).
    //
    // Step 1: Transform vertex to view space
    vec4 viewPos4 = gl_ModelViewMatrix * gl_Vertex;
    vViewPos = viewPos4.xyz;

    // Step 2: Transform view space back to world space using inverse view matrix
    // CRITICAL FIX: Create a separate vec4 for world space transformation
    // DO NOT modify viewPos4.w as it's needed for correct clip space transformation!
    vec4 viewPosForWorldTransform = vec4(viewPos4.xyz, 1.0);
    vec4 worldPos4 = invViewMatrix * viewPosForWorldTransform;
    vWorldPos = worldPos4.xyz / worldPos4.w;  // Perspective divide for safety

    // Step 3: Standard clip space transformation for rendering
    // Use the ORIGINAL viewPos4 with correct w component
    gl_Position = gl_ProjectionMatrix * viewPos4;

    // Store screen position for SSR texture sampling
    vScreenPos = gl_Position;

    // Linear depth for fog calculations
    // Use the actual distance from camera, not clip Z
    vLinearDepth = -vViewPos.z;
}
