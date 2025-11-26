#version 120

// Lake vertex shader with SSR/cubemap support

varying vec2 vTexCoord;
varying vec3 vWorldPos;
varying vec4 vScreenPos;
varying float vLinearDepth;

uniform float near;
uniform float far;

void main()
{
    vTexCoord = gl_MultiTexCoord0.xy;

    // Transform vertex to clip space
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    // World position for reflection calculations
    // gl_ModelViewMatrix includes view transform, so use inverse to get world pos
    vWorldPos = (gl_ModelViewMatrix * gl_Vertex).xyz;

    // Screen position for SSR texture sampling (clip space)
    vScreenPos = gl_Position;

    // Linear depth for fog and other effects
    vLinearDepth = gl_Position.z;
}
