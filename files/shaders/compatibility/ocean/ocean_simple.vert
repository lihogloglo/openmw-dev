#version 120

// Simplified Ocean Vertex Shader with Gerstner Waves
// No FFT required - pure math-based waves

varying vec3 vWorldPos;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vViewPos;

// Required by compatibility
varying vec4 position;
varying float linearDepth;

// Wave parameters
uniform float uTime;
uniform vec3 uCameraPosition;
uniform float uWaveScale;      // Overall wave size multiplier
uniform float uWaveHeight;     // Wave amplitude
uniform float uWaveChoppiness; // Horizontal displacement amount

uniform mat4 osg_ViewMatrixInverse;

// Gerstner wave parameters (4 waves for good variety)
const int NUM_WAVES = 4;

// Wave directions (varied for natural look)
const vec2 WAVE_DIRS[4] = vec2[4](
    vec2(1.0, 0.0),
    vec2(0.7, 0.7),
    vec2(0.0, 1.0),
    vec2(-0.7, 0.7)
);

// Wave properties (wavelength, speed, amplitude multiplier)
const vec3 WAVE_PROPS[4] = vec3[4](
    vec3(60.0, 5.0, 1.0),    // Long slow waves
    vec3(31.0, 4.0, 0.6),    // Medium waves
    vec3(18.0, 3.0, 0.4),    // Shorter waves
    vec3(10.0, 2.5, 0.3)     // Small detail waves
);

// Gerstner wave function
// Returns: .xyz = displacement, .w = derivative for normal calculation
vec4 gerstnerWave(vec2 position, vec2 direction, float wavelength, float speed, float amplitude, float time)
{
    float k = 2.0 * 3.14159 / wavelength;  // Wave number
    float c = speed;                        // Phase speed
    float a = amplitude;
    float f = k * (dot(direction, position) - c * time);

    float Q = uWaveChoppiness / (k * a * float(NUM_WAVES));  // Steepness

    vec4 result;
    result.x = Q * a * direction.x * cos(f);  // X displacement
    result.y = a * sin(f);                     // Y (height) displacement
    result.z = Q * a * direction.y * cos(f);  // Z displacement
    result.w = k * a * cos(f);                // Derivative for normal

    return result;
}

void main()
{
    vec4 localPos = gl_Vertex;
    vTexCoord = gl_MultiTexCoord0.xy;

    // Calculate View Position
    vec4 viewPos = gl_ModelViewMatrix * localPos;

    // Calculate World Position
    vec3 worldPos = (osg_ViewMatrixInverse * viewPos).xyz;
    vec3 cameraPos = osg_ViewMatrixInverse[3].xyz;

    // Distance-based LOD (reduce wave detail far away)
    float dist = length(worldPos.xy - cameraPos.xy);
    float lodFactor = clamp(1.0 - (dist - 500.0) / 2000.0, 0.3, 1.0);

    // Accumulate Gerstner waves
    vec3 totalDisplacement = vec3(0.0);
    vec3 normal = vec3(0.0, 0.0, 1.0);
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 0.0, 1.0);

    for (int i = 0; i < NUM_WAVES; i++)
    {
        vec2 dir = normalize(WAVE_DIRS[i]);
        float wavelength = WAVE_PROPS[i].x * uWaveScale;
        float speed = WAVE_PROPS[i].y;
        float amplitude = WAVE_PROPS[i].z * uWaveHeight * lodFactor;

        vec4 wave = gerstnerWave(worldPos.xz, dir, wavelength, speed, amplitude, uTime);

        totalDisplacement += wave.xyz;

        // Accumulate normal components
        float wa = wave.w;
        tangent += vec3(-dir.x * dir.x * wa, dir.x * wa, -dir.x * dir.y * wa);
        binormal += vec3(-dir.x * dir.y * wa, dir.y * wa, -dir.y * dir.y * wa);
    }

    // Calculate normal from tangent and binormal
    normal = normalize(cross(binormal, tangent));

    // Apply displacement in local space
    vec3 displacedLocalPos = localPos.xyz + totalDisplacement;

    // Transform to view space with displacement
    vec4 displacedViewPos = gl_ModelViewMatrix * vec4(displacedLocalPos, 1.0);

    // Calculate world position for varyings
    vec3 displacedWorldPos = worldPos + totalDisplacement;

    vWorldPos = displacedWorldPos;
    vNormal = gl_NormalMatrix * normal;
    vViewPos = displacedViewPos;

    gl_Position = gl_ProjectionMatrix * displacedViewPos;
    gl_ClipVertex = vViewPos;

    position = displacedViewPos;
    linearDepth = gl_Position.z;
}
