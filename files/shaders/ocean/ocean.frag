#version 120

// Ocean Fragment Shader
// Renders ocean with FFT-based waves

varying vec3 vWorldPos;
varying vec3 vDisplacedPos;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vViewPos;

// Water color and appearance
uniform vec3 uDeepWaterColor;
uniform vec3 uShallowWaterColor;
uniform float uWaterAlpha;

// Reflection/refraction textures
uniform sampler2D uReflectionTexture;
uniform sampler2D uRefractionTexture;

// Foam texture from FFT
uniform sampler2D uFoamCascade0;
uniform sampler2D uFoamCascade1;
uniform float uCascadeTileSize0;
uniform float uCascadeTileSize1;

// Lighting
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;

// Camera
uniform vec3 uCameraPos;

// Fresnel effect
float fresnelSchlick(float cosTheta, float F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Sample foam
float sampleFoam(vec2 worldPosXY)
{
    vec2 uv0 = worldPosXY / uCascadeTileSize0;
    vec2 uv1 = worldPosXY / uCascadeTileSize1;

    float foam0 = texture2D(uFoamCascade0, uv0).r;
    float foam1 = texture2D(uFoamCascade1, uv1).r;

    return max(foam0, foam1 * 0.5);
}

void main()
{
    // Normalize interpolated normal
    vec3 normal = normalize(vNormal);

    // View direction
    vec3 viewDir = normalize(uCameraPos - vDisplacedPos);

    // Fresnel
    float fresnel = fresnelSchlick(max(dot(normal, viewDir), 0.0), 0.02);

    // Water color (deep vs shallow - simplified, no depth map for now)
    vec3 waterColor = mix(uShallowWaterColor, uDeepWaterColor, 0.7);

    // Specular highlight (sun reflection)
    vec3 reflectDir = reflect(-uSunDirection, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 128.0);
    vec3 specular = uSunColor * spec;

    // Foam
    float foam = sampleFoam(vWorldPos.xy);
    vec3 foamColor = vec3(1.0, 1.0, 1.0);

    // Combine
    vec3 finalColor = waterColor * uAmbientColor + specular;
    finalColor = mix(finalColor, foamColor, foam * 0.8);

    // Output
    gl_FragColor = vec4(finalColor, uWaterAlpha);
}
