#version 120

varying vec2 vTexCoord;

uniform float osg_SimulationTime;

void main()
{
    // Simple blue water with slight animation
    float wave = sin(vTexCoord.x * 20.0 + osg_SimulationTime) * 0.5 + 0.5;
    vec3 waterColor = vec3(0.1, 0.3, 0.5) + vec3(0.05) * wave;

    gl_FragColor = vec4(waterColor, 0.7);
}
