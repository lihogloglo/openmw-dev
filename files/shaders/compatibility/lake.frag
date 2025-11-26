#version 120

// Lake fragment shader
// Visuals: Based on ogwatershader/water.frag (6-layer scrolling normals)
// Reflections: SSR (from SSRManager) + Cubemap Fallback

varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 rippleMapUV;

// Uniforms
uniform sampler2D ssrTexture;       // Unit 0: SSR result (RGB=color, A=confidence)
uniform samplerCube environmentMap; // Unit 1: Cubemap fallback
uniform sampler2D normalMap;        // Unit 2: Water normal map
uniform sampler2D rippleMap;        // Unit 3: Ripple map (if used)

uniform vec2 screenRes;
uniform float osg_SimulationTime;
uniform float near;
uniform float far;
uniform int debugMode;

uniform vec3 cameraPos;
uniform mat4 viewMatrix;

// OG Water Constants
const float VISIBILITY = 2500.0;
const vec2 BIG_WAVES = vec2(0.1, 0.1);
const vec2 MID_WAVES = vec2(0.1, 0.1);
const vec2 SMALL_WAVES = vec2(0.1, 0.1);
const float WAVE_CHOPPYNESS = 0.05;
const float WAVE_SCALE = 75.0;
const float BUMP = 0.5;
const float REFL_BUMP = 0.10;
const vec2 WIND_DIR = vec2(0.5, -0.8);
const float WIND_SPEED = 0.2;
const vec3 WATER_COLOR = vec3(0.090195, 0.115685, 0.12745);
const float SPEC_HARDNESS = 256.0;
const float SPEC_BUMPINESS = 5.0;
const float SPEC_BRIGHTNESS = 1.5;

// Helper functions
vec2 normalCoords(vec2 uv, float scale, float speed, float time, float timer1, float timer2, vec3 previousNormal)
{
  return uv * (WAVE_SCALE * scale) + WIND_DIR * time * (WIND_SPEED * speed) -(previousNormal.xy/previousNormal.zz) * WAVE_CHOPPYNESS + vec2(time * timer1,time * timer2);
}

// Fresnel approximation
float fresnel_dielectric(vec3 Incoming, vec3 Normal, float eta)
{
    float c = abs(dot(Incoming, Normal));
    float g = eta * eta - 1.0 + c * c;
    if(g > 0.0)
    {
        g = sqrt(g);
        float A = (g - c) / (g + c);
        float B = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
        return 0.5 * A * A * (1.0 + B * B);
    }
    return 1.0;
}

void main()
{
    // Debug Mode 1: Solid Color (Magenta) - Geometry Check
    if (debugMode == 1) {
        gl_FragData[0] = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    // 1. Calculate Normals (OG Water Style)
    vec2 UV = worldPos.xy / (8192.0*5.0) * 3.0;
    float waterTimer = osg_SimulationTime;

    vec3 normal0 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.05, 0.04, waterTimer, -0.015, -0.005, vec3(0.0,0.0,1.0))).rgb - 1.0;
    vec3 normal1 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.1,  0.08, waterTimer,  0.02,   0.015, normal0)).rgb - 1.0;
    vec3 normal2 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.25, 0.07, waterTimer, -0.04,  -0.03,  normal1)).rgb - 1.0;
    vec3 normal3 = 2.0 * texture2D(normalMap,normalCoords(UV, 0.5,  0.09, waterTimer,  0.03,   0.04,  normal2)).rgb - 1.0;
    vec3 normal4 = 2.0 * texture2D(normalMap,normalCoords(UV, 1.0,  0.4,  waterTimer, -0.02,   0.1,   normal3)).rgb - 1.0;
    vec3 normal5 = 2.0 * texture2D(normalMap,normalCoords(UV, 2.0,  0.7,  waterTimer,  0.1,   -0.06,  normal4)).rgb - 1.0;

    vec3 normal = (normal0 * BIG_WAVES.x + normal1 * BIG_WAVES.y + normal2 * MID_WAVES.x +
                   normal3 * MID_WAVES.y + normal4 * SMALL_WAVES.x + normal5 * SMALL_WAVES.y);
    normal = normalize(vec3(-normal.x * BUMP, -normal.y * BUMP, normal.z));

    // Debug Mode 3: Normals
    if (debugMode == 3) {
        gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }

    // 2. View & Light Vectors
    vec3 viewDir = normalize(worldPos - cameraPos); // Points FROM camera TO surface (OpenMW convention?)
    // Actually, in OG shader: vec3 viewDir = normalize(position.xyz - cameraPos.xyz);
    // And fresnel uses viewDir.
    
    // Sun direction (hardcoded for now or passed via uniform if available)
    vec3 sunDir = normalize(vec3(0.5, 0.5, 1.0)); 
    
    // 3. Fresnel
    float ior = (cameraPos.z > worldPos.z) ? (1.333/1.0) : (1.0/1.333);
    float fresnel = clamp(fresnel_dielectric(viewDir, normal, ior), 0.0, 1.0);

    // 4. Reflections (SSR + Cubemap)
    vec2 screenCoords = gl_FragCoord.xy / screenRes;
    vec2 screenCoordsOffset = normal.xy * REFL_BUMP;
    
    // Sample SSR
    // SSR texture contains: RGB = reflection color, A = confidence
    vec4 ssrSample = texture2D(ssrTexture, screenCoords + screenCoordsOffset);
    vec3 ssrColor = ssrSample.rgb;
    float ssrConfidence = ssrSample.a;

    // Sample Cubemap (Fallback)
    vec3 reflectDir = reflect(viewDir, normal);
    vec3 cubemapColor = textureCube(environmentMap, reflectDir).rgb;

    // Blend SSR and Cubemap
    vec3 reflection = mix(cubemapColor, ssrColor, ssrConfidence);

    // Debug Mode 4: SSR Only
    if (debugMode == 4) {
        gl_FragData[0] = vec4(ssrColor, 1.0);
        return;
    }
    // Debug Mode 5: Cubemap Only
    if (debugMode == 5) {
        gl_FragData[0] = vec4(cubemapColor, 1.0);
        return;
    }
    // Debug Mode 6: SSR Confidence
    if (debugMode == 6) {
        gl_FragData[0] = vec4(ssrConfidence, ssrConfidence, ssrConfidence, 1.0);
        return;
    }

    // 5. Specular (Sun)
    vec3 specNormal = normalize(vec3(normal.x * SPEC_BUMPINESS, normal.y * SPEC_BUMPINESS, normal.z));
    vec3 viewReflectDir = reflect(viewDir, specNormal);
    float phongTerm = max(dot(viewReflectDir, sunDir), 0.0);
    float specular = pow(atan(phongTerm * 1.55), SPEC_HARDNESS) * SPEC_BRIGHTNESS;
    
    // 6. Final Color Composition
    // Mix Water Color and Reflection based on Fresnel
    vec3 finalColor = mix(WATER_COLOR, reflection, (1.0 + fresnel) * 0.5);
    finalColor += vec3(specular); // Add specular

    float alpha = clamp(fresnel * 6.0 + specular, 0.6, 1.0); // Base alpha + fresnel

    gl_FragData[0] = vec4(finalColor, alpha);
}
