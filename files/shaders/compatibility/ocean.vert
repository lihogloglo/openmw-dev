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
uniform vec3 cameraPosition; // Added for distance calculation
uniform vec4 mapScales[4]; // xy = scale for each cascade

void main(void)
{
    position = gl_Vertex;
    texCoord = gl_MultiTexCoord0.xy;

    // Apply FFT displacement
    vec3 totalDisplacement = vec3(0.0);

    // Sample displacement from cascades
    // mapScales format: vec4(uvScale, uvScale, displacementScale, normalScale)
    vec3 vertPos = position.xyz;

    // Displacement sampling with per-cascade amplitude scaling
    // This creates "wavelets inside small waves inside big waves" effect
    for (int i = 0; i < numCascades && i < 4; ++i) {
        vec2 uv = (vertPos.xy + nodePosition.xy) * mapScales[i].x;
        vec3 disp = texture(displacementMap, vec3(uv, float(i))).xyz;
        
        // Distance-based falloff for displacement
        // Suppress small waves at distance to prevent aliasing/shimmering
        float dist = length(vertPos.xy + nodePosition.xy - cameraPosition.xy);
        float falloff = 1.0;
        
        // Cascade 0 (50m) fades out after 1000m (in MW units: 72530)
        // We use MW units for distance comparison since 'dist' is in MW units
        // 500m = 36265 units, 1000m = 72530 units
        if (i == 0) falloff = clamp(1.0 - (dist - 36265.0) / 36265.0, 0.0, 1.0);
        // Cascade 1 (100m) fades out after 3000m (in MW units: 217590)
        // 2000m = 145060 units, 1000m range = 72530 units
        else if (i == 1) falloff = clamp(1.0 - (dist - 145060.0) / 72530.0, 0.0, 1.0);
        
        // Apply per-cascade displacement scale (larger cascades = bigger waves)
        totalDisplacement += disp * mapScales[i].z * falloff;
    }

    vertPos += totalDisplacement;

    worldPos = vertPos + nodePosition;

    gl_Position = modelToClip(vec4(vertPos, 1.0));

    vec4 viewPos = modelToView(vec4(vertPos, 1.0));
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    setupShadowCoords(viewPos, normalize((gl_NormalMatrix * gl_Normal).xyz));
}
