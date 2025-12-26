#version 120

// Simple vertex shader for displacement map rendering
// Just passes through position and texture coordinates

varying vec2 uv;

void main()
{
    gl_Position = gl_Vertex;
    uv = gl_MultiTexCoord0.xy;
}
