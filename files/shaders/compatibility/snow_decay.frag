#version 120

// Fragment shader for decaying deformation over time
// Applies to entire texture as a post-process

#include "lib/terrain/deformation.glsl"

varying vec2 texCoord;

uniform sampler2D deformationMap;
uniform float decayFactor;  // Multiplier per frame (e.g., 0.99 = 1% decay)

void main()
{
    // Sample current deformation
    float currentDeformation = texture2D(deformationMap, texCoord).r;

    // Apply decay using material-specific rate
    // For now, use snow decay rate (material type 1)
    float materialDecayRate = getDecayRate(1);
    float newDeformation = currentDeformation * materialDecayRate * decayFactor;

    // Output decayed value
    gl_FragColor = vec4(newDeformation, 0.0, 0.0, 1.0);
}
