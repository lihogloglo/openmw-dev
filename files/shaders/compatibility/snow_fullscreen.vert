#version 120

// Simple fullscreen quad vertex shader for RTT operations

attribute vec3 osg_Vertex;
attribute vec2 osg_MultiTexCoord0;

uniform mat4 osg_ModelViewProjectionMatrix;

varying vec2 texCoord;

void main()
{
    gl_Position = osg_ModelViewProjectionMatrix * vec4(osg_Vertex, 1.0);
    texCoord = osg_MultiTexCoord0;
}
