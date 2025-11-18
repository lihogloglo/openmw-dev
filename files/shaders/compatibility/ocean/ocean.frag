#version 120

// Ocean Fragment Shader
// Renders ocean with FFT-based waves and improved lighting

varying vec3 vWorldPos;
varying vec3 vDisplacedPos;
varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vViewPos;

// Water color and appearance
uniform vec3 uDeepWaterColor;
uniform vec3 uShallowWaterColor;
uniform float uWaterAlpha;

// Foam texture from FFT
uniform sampler2D uFoamCascade0;
uniform sampler2D uFoamCascade1;
uniform float uCascadeTileSize0;
uniform float uCascadeTileSize1;

// Schlick's Fresnel approximation with better parameterization
float fresnelSchlick(float cosTheta, float F0)
{
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
    return fresnel;
}

// Sample foam with better blending
float sampleFoam(vec2 worldPosXY)
{
    vec2 uv0 = worldPosXY / uCascadeTileSize0;
    vec2 uv1 = worldPosXY / uCascadeTileSize1;

    float foam0 = texture2D(uFoamCascade0, uv0).r;
    float foam1 = texture2D(uFoamCascade1, uv1).r;

    // Blend cascades with proper weighting
    return max(foam0, foam1 * 0.5);
}

// Improved specular using Blinn-Phong for sharper highlights
float blinnPhongSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess)
{
    vec3 halfDir = normalize(lightDir + viewDir);
    return pow(max(dot(normal, halfDir), 0.0), shininess);
}

void main()
{
    // Normalize interpolated normal
    vec3 normal = normalize(vNormal);

    // View direction (from fragment to camera)
    vec3 viewDir = normalize(-vViewPos.xyz);

    // Sun direction (slightly tilted for better lighting)
    vec3 sunDir = normalize(vec3(0.3, 0.3, 1.0));
    vec3 sunColor = vec3(1.0, 0.95, 0.85);

    // Fresnel effect (water has F0 ~0.02 at normal incidence)
    float fresnel = fresnelSchlick(max(dot(normal, viewDir), 0.0), 0.02);

    // Sky color for reflection (approximate)
    vec3 skyColor = vec3(0.5, 0.7, 1.0);

    // Water color with depth approximation (darker when looking down)
    float viewAngle = dot(normal, viewDir);
    float depthFactor = smoothstep(0.0, 1.0, 1.0 - viewAngle);
    vec3 waterColor = mix(uShallowWaterColor, uDeepWaterColor, depthFactor * 0.8);

    // Ambient lighting (underwater and sky)
    vec3 ambient = waterColor * 0.4 + vec3(0.2, 0.25, 0.3);

    // Diffuse lighting from sun
    float diffuse = max(dot(normal, sunDir), 0.0);
    vec3 diffuseColor = waterColor * sunColor * diffuse * 0.6;

    // Specular highlight using Blinn-Phong
    float specularStrength = blinnPhongSpecular(normal, sunDir, viewDir, 256.0);
    vec3 specular = sunColor * specularStrength * 1.5;

    // Subsurface scattering approximation (light through waves)
    float subsurface = max(dot(normal, sunDir), 0.0) * 0.3;
    vec3 subsurfaceColor = uShallowWaterColor * subsurface;

    // Foam
    float foam = sampleFoam(vWorldPos.xy);
    vec3 foamColor = vec3(1.0, 1.0, 1.0);

    // Make foam more prominent at wave crests
    float foamHighlight = foam * 0.9;

    // Combine all lighting components
    vec3 finalColor = ambient + diffuseColor + subsurfaceColor;

    // Add specular highlights
    finalColor += specular;

    // Mix in sky reflection based on Fresnel
    finalColor = mix(finalColor, skyColor, fresnel * 0.6);

    // Add foam on top
    finalColor = mix(finalColor, foamColor, foamHighlight);

    // Depth-based alpha (more transparent when viewing at grazing angles)
    float alpha = mix(uWaterAlpha * 0.7, uWaterAlpha, viewAngle);

    // Output with proper alpha
    gl_FragColor = vec4(finalColor, alpha);
}
