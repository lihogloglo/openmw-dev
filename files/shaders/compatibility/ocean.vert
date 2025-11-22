#version 330 compatibility

// Don't include lib/core/vertex.h.glsl due to version conflicts
// Manually define the required functions
uniform mat4 projectionMatrix;

vec4 modelToView(vec4 pos)
{
    return gl_ModelViewMatrix * pos;
}

vec4 modelToClip(vec4 pos)
{
    return projectionMatrix * modelToView(pos);
}

varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 texCoord;

#include "shadows_vertex.glsl"
#include "lib/view/depth.glsl"

uniform vec3 nodePosition;
uniform sampler2DArray displacementMap;
uniform int numCascades;
uniform vec4 mapScales[4]; // xy = scale for each cascade

void main(void)
{
    position = gl_Vertex;
    texCoord = gl_MultiTexCoord0.xy;

    // Apply FFT displacement
    vec3 totalDisplacement = vec3(0.0);

    // Sample displacement from cascades
    // Note: In a full implementation, we'd sample all cascades and blend them
    // For now, simplified version
    vec3 vertPos = position.xyz;

    // Displacement sampling
    for (int i = 0; i < numCascades && i < 4; ++i) {
        vec2 uv = (vertPos.xy + nodePosition.xy) * mapScales[i].x;
        vec3 disp = texture(displacementMap, vec3(uv, float(i))).xyz;
        totalDisplacement += disp;
    }
    vertPos += totalDisplacement;

    worldPos = vertPos + nodePosition;

    gl_Position = modelToClip(vec4(vertPos, 1.0));

    vec4 viewPos = modelToView(vec4(vertPos, 1.0));
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    setupShadowCoords(viewPos, normalize((gl_NormalMatrix * gl_Normal).xyz));
}
