#version 120

// Simple vertex shader for rendering footprints to deformation texture
// Each footprint is rendered as a small quad with radial falloff

attribute vec3 osg_Vertex;
attribute vec2 osg_MultiTexCoord0;

uniform mat4 osg_ModelViewProjectionMatrix;

varying vec2 texCoord;

void main()
{
    gl_Position = osg_ModelViewProjectionMatrix * vec4(osg_Vertex, 1.0);
    texCoord = osg_MultiTexCoord0;
}
