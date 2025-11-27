#version 120

// Lake fragment shader
// Visuals: Based on ogwatershader/water.frag (6-layer scrolling normals)
// Reflections: SSR (raymarching) + Cubemap Fallback

varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 rippleMapUV;

uniform sampler2D sceneColorBuffer;
uniform samplerCube environmentMap;
uniform sampler2D normalMap;
uniform sampler2D depthBuffer;

uniform vec2 screenRes;
uniform float osg_SimulationTime;
uniform float near;
uniform float far;
uniform int debugMode;
uniform float ssrMixStrength;
uniform bool reverseZ;
uniform vec3 cameraPos;

const vec2 BIG_WAVES = vec2(0.1, 0.1);
const vec2 MID_WAVES = vec2(0.1, 0.1);
const vec2 SMALL_WAVES = vec2(0.1, 0.1);
const float WAVE_CHOPPYNESS = 0.05;
const float WAVE_SCALE = 75.0;
const float BUMP = 0.5;
const vec2 WIND_DIR = vec2(0.5, -0.8);
const float WIND_SPEED = 0.2;
const vec3 WATER_COLOR = vec3(0.090195, 0.115685, 0.12745);
const float SPEC_HARDNESS = 256.0;
const float SPEC_BUMPINESS = 5.0;
const float SPEC_BRIGHTNESS = 1.5;

const float SSR_MAX_DISTANCE = 8192.0;
const int SSR_MAX_STEPS = 32;
const float SSR_THICKNESS = 0.5;
const float SSR_FADE_START = 0.8;

vec2 normalCoords(vec2 uv, float scale, float speed, float time, float timer1, float timer2, vec3 prevNormal)
{
    return uv * (WAVE_SCALE * scale) + WIND_DIR * time * (WIND_SPEED * speed) 
           - (prevNormal.xy / prevNormal.zz) * WAVE_CHOPPYNESS + vec2(time * timer1, time * timer2);
}

float fresnel_dielectric(vec3 I, vec3 N, float eta)
{
    float c = abs(dot(I, N));
    float g = eta * eta - 1.0 + c * c;
    if (g > 0.0) {
        g = sqrt(g);
        float A = (g - c) / (g + c);
        float B = (c * (g + c) - 1.0) / (c * (g - c) + 1.0);
        return 0.5 * A * A * (1.0 + B * B);
    }
    return 1.0;
}

float linearizeDepth(float depth, float nearPlane, float farPlane)
{
    float d = depth;
    if (reverseZ)
        d = 1.0 - d;
        
    return nearPlane * farPlane / (farPlane + d * (nearPlane - farPlane));
}

float screenEdgeFade(vec2 uv)
{
    vec2 fade = smoothstep(vec2(0.0), vec2(1.0 - SSR_FADE_START), uv) 
              * (1.0 - smoothstep(vec2(SSR_FADE_START), vec2(1.0), uv));
    return fade.x * fade.y;
}

vec4 traceSSR(vec3 viewPos, vec3 viewNormal)
{
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = reflect(viewDir, viewNormal);
    
    if (reflectDir.z > 0.0) {
        return vec4(0.0);
    }
    
    vec3 rayPos = viewPos;
    float stepSize = SSR_MAX_DISTANCE / float(SSR_MAX_STEPS);
    
    for (int i = 0; i < SSR_MAX_STEPS; i++) {
        rayPos += reflectDir * stepSize;
        
        vec4 clipPos = gl_ProjectionMatrix * vec4(rayPos, 1.0);
        vec2 rayUV = (clipPos.xy / clipPos.w) * 0.5 + 0.5;
        
        if (rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0) break;
        
        float sceneDepth = texture2D(depthBuffer, rayUV).r;
        float sceneLinearDepth = linearizeDepth(sceneDepth, near, far);
        float rayLinearDepth = -rayPos.z;
        float depthDiff = rayLinearDepth - sceneLinearDepth;
        
        if (depthDiff > 0.0 && depthDiff < SSR_THICKNESS) {
            vec3 hitColor = texture2D(sceneColorBuffer, rayUV).rgb;
            return vec4(hitColor, screenEdgeFade(rayUV));
        }
    }
    return vec4(0.0);
}

void main()
{
    vec2 screenUV = gl_FragCoord.xy / screenRes;
    
    if (debugMode == 1) { gl_FragData[0] = vec4(1.0, 0.0, 1.0, 1.0); return; }
    if (debugMode == 2) { gl_FragData[0] = vec4(fract(worldPos / 10000.0), 1.0); return; }
    
    float waterTimer = osg_SimulationTime;
    vec2 UV = worldPos.xy / (8192.0 * 5.0) * 3.0;
    
    vec3 n0 = 2.0 * texture2D(normalMap, normalCoords(UV, 0.05, 0.04, waterTimer, -0.015, -0.005, vec3(0,0,1))).rgb - 1.0;
    vec3 n1 = 2.0 * texture2D(normalMap, normalCoords(UV, 0.1,  0.08, waterTimer,  0.02,   0.015, n0)).rgb - 1.0;
    vec3 n2 = 2.0 * texture2D(normalMap, normalCoords(UV, 0.25, 0.07, waterTimer, -0.04,  -0.03,  n1)).rgb - 1.0;
    vec3 n3 = 2.0 * texture2D(normalMap, normalCoords(UV, 0.5,  0.09, waterTimer,  0.03,   0.04,  n2)).rgb - 1.0;
    vec3 n4 = 2.0 * texture2D(normalMap, normalCoords(UV, 1.0,  0.4,  waterTimer, -0.02,   0.1,   n3)).rgb - 1.0;
    vec3 n5 = 2.0 * texture2D(normalMap, normalCoords(UV, 2.0,  0.7,  waterTimer,  0.1,   -0.06,  n4)).rgb - 1.0;
    
    vec3 normal = n0 * BIG_WAVES.x + n1 * BIG_WAVES.y + n2 * MID_WAVES.x +
                  n3 * MID_WAVES.y + n4 * SMALL_WAVES.x + n5 * SMALL_WAVES.y;
    normal = normalize(vec3(-normal.x * BUMP, -normal.y * BUMP, normal.z));
    
    if (debugMode == 3) { gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0); return; }
    
    vec3 viewDir = normalize(position.xyz);
    vec3 viewPos = position.xyz;
    mat3 normalMatrix = transpose(mat3(gl_ModelViewMatrixInverse));
    vec3 viewNormal = normalize(normalMatrix * normal);
    
    float ior = (cameraPos.z > worldPos.z - 5.0) ? (1.333 / 1.0) : (1.0 / 1.333);
    float fresnel = clamp(fresnel_dielectric(viewDir, normal, ior), 0.0, 1.0);
    
    vec4 ssrResult = traceSSR(viewPos, viewNormal);
    vec3 ssrColor = ssrResult.rgb;
    float ssrConfidence = ssrResult.a * ssrMixStrength;
    
    vec3 reflectDir = reflect(viewDir, normal);
    vec3 cubemapColor = textureCube(environmentMap, reflectDir).rgb;
    vec3 reflection = mix(cubemapColor, ssrColor, ssrConfidence);
    
    if (debugMode == 4) { gl_FragData[0] = vec4(ssrColor, 1.0); return; }
    if (debugMode == 5) { gl_FragData[0] = vec4(cubemapColor, 1.0); return; }
    if (debugMode == 6) { gl_FragData[0] = vec4(vec3(ssrConfidence), 1.0); return; }
    if (debugMode == 7) { gl_FragData[0] = vec4(screenUV, 0.0, 1.0); return; }
    if (debugMode == 8) {
        float d = linearizeDepth(texture2D(depthBuffer, screenUV).r, near, far) / far;
        gl_FragData[0] = vec4(vec3(d), 1.0); return;
    }
    
    vec3 sunWorldDir = normalize((gl_ModelViewMatrixInverse * vec4(gl_LightSource[0].position.xyz, 0.0)).xyz);
    float sunFade = length(gl_LightModel.ambient.xyz);
    
    vec3 specNormal = normalize(vec3(normal.x * SPEC_BUMPINESS, normal.y * SPEC_BUMPINESS, normal.z));
    float phongTerm = max(dot(reflect(viewDir, specNormal), sunWorldDir), 0.0);
    float specular = clamp(pow(atan(phongTerm * 1.55), SPEC_HARDNESS) * SPEC_BRIGHTNESS, 0.0, 1.0);
    
    vec3 waterColorTinted = WATER_COLOR * sunFade;
    vec3 finalColor = mix(waterColorTinted, reflection, (1.0 + fresnel) * 0.5);
    finalColor += specular * gl_LightSource[0].specular.rgb;
    float alpha = clamp(fresnel * 6.0 + specular, 0.6, 1.0);
    
    if (debugMode == 9) { gl_FragData[0] = vec4(waterColorTinted, 0.7); return; }
    
    gl_FragData[0] = vec4(finalColor, alpha);
}
