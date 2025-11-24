#version 330 compatibility

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/fragment.h.glsl"

// Provide reflection/refraction sampling implementations for version 330
uniform sampler2D reflectionMap;

vec4 sampleReflectionMap(vec2 uv)
{
    return texture2D(reflectionMap, uv);
}

#if @waterRefraction
uniform sampler2D refractionMap;
uniform sampler2D refractionDepthMap;

vec4 sampleRefractionMap(vec2 uv)
{
    return texture2D(refractionMap, uv);
}

float sampleRefractionDepthMap(vec2 uv)
{
    return texture2D(refractionDepthMap, uv).x;
}
#endif

// Ocean water fragment shader - FFT-based water rendering
// Based on GodotOceanWaves

// Water rendering constants (matching original water shader)
const float VISIBILITY = 2500.0;
const float VISIBILITY_DEPTH = VISIBILITY * 1.5;
const float DEPTH_FADE = 0.15;
// Water color matching Godot reference: Color(0.1, 0.15, 0.18) in sRGB
// OpenMW expects sRGB color values (not linear), so use values directly from Godot
const vec3 WATER_COLOR = vec3(0.1, 0.15, 0.18);

// Reflection/refraction distortion
const float REFL_BUMP = 0.20;  // reflection distortion amount (increased to hide reflection map seams)
const float REFR_BUMP = 0.12;  // refraction distortion amount (increased from 0.07)
const float BUMP_SUPPRESS_DEPTH = 300.0; // suppress distortion at shores

varying vec4 position;
varying float linearDepth;
varying vec3 worldPos;
varying vec2 texCoord;
varying float waveHeight; // Y component of displacement from vertex shader

uniform sampler2DArray normalMap;
uniform sampler2DArray spectrumMap; // DEBUG
uniform int numCascades;
uniform vec4 mapScales[4];
uniform vec3 sunDir;
uniform vec3 sunColor;

uniform float osg_SimulationTime;
uniform float near;
uniform float far;
uniform vec2 screenRes;

// Debug visualization toggle
uniform int debugVisualizeCascades; // 0 = off, 1 = on
uniform int debugVisualizeLOD;      // 0 = off, 1 = on

// Camera position in world space (for cascade selection)
uniform vec3 cameraPosition;
uniform vec3 nodePosition;

#define PER_PIXEL_LIGHTING 1

#include "shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "fog.glsl"
#include "lib/water/fresnel.glsl"
#include "lib/view/depth.glsl"

// ============================================================================
// PBR Helper Functions for Ocean Shading
// Based on Godot ocean shader and "Wakes, Explosions and Lighting" GDC talk
// ============================================================================

const float PI = 3.14159265359;
const float REFLECTANCE = 0.02; // Air to water reflectance (eta=1.33)
const float MW_UNITS_TO_METERS = 1.0 / 72.53; // Conversion factor for distance calculations

// GGX microfacet distribution function
// Source: https://github.com/godotengine/godot/blob/7b56111c297f24304eb911fe75082d8cdc3d4141/drivers/gles3/shaders/scene.glsl#L995
float ggx_distribution(float cos_theta, float alpha) {
    float a_sq = alpha * alpha;
    float d = 1.0 + (a_sq - 1.0) * cos_theta * cos_theta;
    return a_sq / (PI * d * d);
}

// Smith masking-shadowing function for GGX
float smith_masking_shadowing(float cos_theta, float alpha) {
    // Approximate: cos_theta / (alpha * tan(acos(cos_theta)))
    float a = cos_theta / (alpha * sqrt(1.0 - cos_theta * cos_theta));
    float a_sq = a * a;
    return a < 1.6 ? (1.0 - 1.259*a + 0.396*a_sq) / (3.535*a + 2.181*a_sq) : 0.0;
}

// Fresnel-Schlick approximation (modified for water)
float fresnel_schlick(float cos_theta, float roughness) {
    return mix(
        pow(1.0 - cos_theta, 5.0 * exp(-2.69 * roughness)) / (1.0 + 22.7 * pow(roughness, 1.5)),
        1.0,
        REFLECTANCE
    );
}

// ============================================================================
// Bicubic Texture Filtering for Sharp Wave Details
// Source: GPU Gems 2 Chapter 20 - Fast Third-Order Texture Filtering
// Source: Godot ocean shader water.gdshader:41-84
// ============================================================================

// Filter weights for a cubic B-spline
vec4 cubic_weights(float a) {
    float a2 = a * a;
    float a3 = a2 * a;

    float w0 = -a3      + a2*3.0 - a*3.0 + 1.0;
    float w1 =  a3*3.0  - a2*6.0         + 4.0;
    float w2 = -a3*3.0  + a2*3.0 + a*3.0 + 1.0;
    float w3 =  a3;
    return vec4(w0, w1, w2, w3) / 6.0;
}

// Performs bicubic B-spline filtering on sampler2DArray
vec4 texture_bicubic(sampler2DArray sampler, vec3 uvw) {
    // Get texture dimensions
    ivec3 dims = textureSize(sampler, 0);
    vec2 dims_inv = 1.0 / vec2(dims.xy);

    // Shift coordinates to texel centers
    uvw.xy = uvw.xy * vec2(dims.xy) + 0.5;

    vec2 fuv = fract(uvw.xy);
    vec4 wx = cubic_weights(fuv.x);
    vec4 wy = cubic_weights(fuv.y);

    // Calculate optimized sampling positions
    vec4 g = vec4(wx.xz + wx.yw, wy.xz + wy.yw);
    vec4 h = (vec4(wx.yw, wy.yw) / g + vec2(-1.5, 0.5).xyxy + floor(uvw.xy).xxyy) * dims_inv.xxyy;
    vec2 w = g.xz / (g.xz + g.yw);

    // Perform 4 bilinear samples (instead of 16 point samples)
    return mix(
        mix(texture(sampler, vec3(h.yw, uvw.z)), texture(sampler, vec3(h.xw, uvw.z)), w.x),
        mix(texture(sampler, vec3(h.yz, uvw.z)), texture(sampler, vec3(h.xz, uvw.z)), w.x),
        w.y
    );
}

void main(void)
{
    vec2 screenCoords = gl_FragCoord.xy / screenRes;
    float shadow = unshadowedLightRatio(linearDepth);

    // Sample gradients from FFT cascade using snapped world position
    // mapScales format: vec4(uvScale, uvScale, displacementScale, normalScale)
    // normalMap stores: vec4(gradient.x, gradient.y, dhx_dx, foam)
    vec3 gradient = vec3(0.0);
    float foam = 0.0;

    // worldPos is calculated in vertex shader as: vec3(worldPosXY, vertPos.z) + nodePosition
    // Since nodePosition is (0, 0, height), worldPos.xy already contains the snapped world XY
    // We just need to subtract the Z offset to get the actual 2D world position
    vec2 worldPosXY = worldPos.xy;

    float distToCamera = length(worldPosXY - cameraPosition.xy);

    // Get normal map size for bicubic filtering calculation
    int map_size = textureSize(normalMap, 0).x;

    for (int i = 0; i < numCascades && i < 4; ++i) {
        vec2 uv = worldPosXY * mapScales[i].x;
        vec3 coords = vec3(uv, float(i));

        // Calculate pixels-per-meter for adaptive filtering
        // Godot line 80: float ppm = map_size * min(scales.x, scales.y);
        // In our case, scales.x = scales.y = mapScales[i].x (UV scale)
        float ppm = float(map_size) * mapScales[i].x;

        // Mix between bicubic (high detail) and bilinear (low detail) based on pixel density
        // This is dependent on the tile size as well as normal map resolution
        // Godot line 83: gradient += mix(texture_bicubic(normals, coords), texture(normals, coords), min(1.0, ppm*0.1))
        vec4 normalSample = mix(
            texture_bicubic(normalMap, coords),
            texture(normalMap, coords),
            min(1.0, ppm * 0.1)
        );

        // Apply per-cascade normal scale to gradient
        // Godot line 83: gradient += ... * vec3(scales.ww, 1.0);
        // scales.w is the normal scale, which is mapScales[i].w for us
        gradient.xy += normalSample.xy * mapScales[i].w;
        foam += normalSample.w;
    }

    // Distance-based normal strength falloff
    // Godot line 89: gradient *= mix(0.015, normal_strength, exp(-dist*0.0175));
    // This blends normals to nearly flat (0.015) at far distances using exponential falloff
    // This is the KEY to preventing overly detailed/bumpy far ocean and over-bright speculars
    // IMPORTANT: Godot's falloff rate works on METERS, so convert MW units to meters first
    // Halved the falloff rate (0.0175 -> 0.00875) to make falloff happen 2x farther away
    const float NORMAL_STRENGTH = 1.0;
    float distToCamera_meters = distToCamera * MW_UNITS_TO_METERS;
    float normalFalloff = mix(0.015, NORMAL_STRENGTH, exp(-distToCamera_meters * 0.00875));
    gradient *= normalFalloff;

    // Reconstruct normal from gradient (as done in Godot water shader)
    vec3 normal = normalize(vec3(-gradient.x, 1.0, -gradient.y));

    // Debug: Visualize cascade coverage with color coding
    if (debugVisualizeCascades == 1) {
        // Cascade colors: Red, Green, Blue, Yellow
        vec3 cascadeColors[4] = vec3[4](
            vec3(1.0, 0.0, 0.0),  // Cascade 0: Red (finest detail, 50m tiles)
            vec3(0.0, 1.0, 0.0),  // Cascade 1: Green (100m tiles)
            vec3(0.0, 0.5, 1.0),  // Cascade 2: Blue (200m tiles)
            vec3(1.0, 1.0, 0.0)   // Cascade 3: Yellow (broadest, 400m tiles)
        );

        // Use camera position uniform (already in world space)
        float distToCamera = length(worldPos.xy - cameraPosition.xy);

        // Select cascade based on distance from camera
        // Thresholds based on cascade tile sizes (in MW units):
        // Cascade 0 (red):    0-7,253 units (50-100m tiles)
        // Cascade 1 (green):  7,253-14,506 units (100-200m tiles)
        // Cascade 2 (blue):   14,506-29,012 units (200-400m tiles)
        // Cascade 3 (yellow): 29,012+ units (400m+ tiles)
        int selectedCascade = 3;
        if (distToCamera < 7253.0) selectedCascade = 0;
        else if (distToCamera < 14506.0) selectedCascade = 1;
        else if (distToCamera < 29012.0) selectedCascade = 2;

        vec3 debugColor = cascadeColors[selectedCascade];

        // Draw grid for selected cascade only
        vec2 uv = worldPos.xy * mapScales[selectedCascade].x;
        vec2 uvFrac = fract(uv);

        // Grid lines for this cascade
        float gridLineX = step(0.99, uvFrac.x) + step(uvFrac.x, 0.01);
        float gridLineY = step(0.99, uvFrac.y) + step(uvFrac.y, 0.01);
        float isOnGrid = max(gridLineX, gridLineY);

        if (isOnGrid > 0.5) {
            debugColor = vec3(0.0); // Black grid lines
        }

        gl_FragData[0] = vec4(debugColor, 0.9);
        applyShadowDebugOverlay();
        return;
    }

    // Debug: Visualize LOD rings with grid density
    if (debugVisualizeLOD == 1) {
        float dist = length(worldPos.xy - cameraPosition.xy);
        float gridSize = 0.0;
        vec3 ringColor = vec3(1.0);

        // Match ring radii and grid sizes from ocean.cpp
        if (dist < 1000.0) {
            gridSize = 3.90625; // Ring 0
            ringColor = vec3(1.0, 0.0, 0.0); // Red
        } else if (dist < 1813.0) {
            gridSize = 14.164; // Ring 1
            ringColor = vec3(1.0, 0.5, 0.0); // Orange
        } else if (dist < 3626.0) {
            gridSize = 56.664; // Ring 2
            ringColor = vec3(1.0, 1.0, 0.0); // Yellow
        } else if (dist < 7253.0) {
            gridSize = 226.656; // Ring 3
            ringColor = vec3(0.0, 1.0, 0.0); // Green
        } else if (dist < 14506.0) {
            gridSize = 906.625; // Ring 4
            ringColor = vec3(0.0, 1.0, 1.0); // Cyan
        } else if (dist < 29012.0) {
            gridSize = 1813.25; // Ring 5
            ringColor = vec3(0.0, 0.0, 1.0); // Blue
        } else if (dist < 58024.0) {
            gridSize = 3626.5; // Ring 6
            ringColor = vec3(0.5, 0.0, 1.0); // Purple
        } else if (dist < 116048.0) {
            gridSize = 7253.0; // Ring 7
            ringColor = vec3(1.0, 0.0, 1.0); // Magenta
        } else if (dist < 232096.0) {
            gridSize = 14506.0; // Ring 8
            ringColor = vec3(0.5, 0.5, 0.5); // Grey
        } else {
            gridSize = 58024.0; // Ring 9
            ringColor = vec3(0.2, 0.2, 0.2); // Dark Grey
        }

        // Draw grid lines
        vec2 gridUV = worldPos.xy / gridSize;
        vec2 gridFrac = fract(gridUV);
        float lineThickness = 0.02; // Adjust for visibility
        float gridX = step(1.0 - lineThickness, gridFrac.x) + step(gridFrac.x, lineThickness);
        float gridY = step(1.0 - lineThickness, gridFrac.y) + step(gridFrac.y, lineThickness);
        float isGrid = max(gridX, gridY);

        vec3 finalDebugColor = mix(ringColor * 0.2, vec3(1.0), isGrid); // Faint background, white grid
        gl_FragData[0] = vec4(finalDebugColor, 1.0);
        applyShadowDebugOverlay();
        return;
    }

    // Distance-based foam intensity falloff
    // Godot line 86: foam_factor = smoothstep(0.0, 1.0, gradient.z*0.75) * exp(-dist*0.0075);
    // This makes foam fade out at distance for a cleaner horizon
    // IMPORTANT: Godot's falloff rate works on METERS, so convert MW units to meters first
    // Halved the falloff rate (0.0075 -> 0.00375) to make falloff happen 2x farther away
    float foamFalloff = exp(-distToCamera_meters * 0.00375);
    foam *= foamFalloff;

    // Clamp foam to reasonable range
    foam = clamp(foam * 0.75, 0.0, 1.0); // Scale down since we accumulate from multiple cascades

    // ========================================================================
    // PBR LIGHTING MODEL
    // ========================================================================

    // Get sun fade early (needed for water color modulation)
    float sunFade = length(gl_LightModel.ambient.xyz);

    // Mix water color with foam color
    // Water color darkens at night, foam stays bright (like original water shader)
    // Using white foam - Godot's beige (0.73, 0.67, 0.62) looked too brown in OpenMW's lighting
    const vec3 FOAM_COLOR = vec3(1.0, 1.0, 1.0);
    float foamFactor = smoothstep(0.0, 1.0, foam);
    vec3 waterColorModulated = WATER_COLOR * sunFade;
    vec3 albedo = mix(waterColorModulated, FOAM_COLOR, foamFactor);

    // Get view direction (camera position in view space is at origin)
    // Extract camera world position from inverse view matrix
    vec3 cameraPos = (gl_ModelViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    // View direction points FROM camera TO fragment (same convention as original water shader)
    vec3 viewDir = normalize(worldPos - cameraPos);

    // Get sun direction in world space (same as original water shader)
    vec3 sunWorldDir = normalize((gl_ModelViewMatrixInverse * vec4(lcalcPosition(0).xyz, 0.0)).xyz);

    // Get sun color and visibility (same method as old water shader)
    vec4 sunSpec = lcalcSpecular(0);

    // Sun visibility factor for specular fading (like original water shader)
    const float SUN_SPEC_FADING_THRESHOLD = 0.15;
    float sunVisibility = min(1.0, sunSpec.a / SUN_SPEC_FADING_THRESHOLD);

    // Sun color - use sunSpec.rgb directly (it already contains the sun color)
    // Apply sunFade to modulate by time of day
    vec3 sunColor = sunSpec.rgb;

    // Calculate roughness (foam is rougher than water)
    // Matching Godot reference: roughness = 0.4 (smooth water, mirror-like reflections)
    const float BASE_ROUGHNESS = 0.4;
    float roughness = BASE_ROUGHNESS;

    // Calculate Fresnel
    // Godot line 92: fresnel = mix(pow(1.0 - dot(VIEW, NORMAL), ...), 1.0, REFLECTANCE);
    // In Godot, VIEW points toward camera, so dot(VIEW, NORMAL) is positive when looking at surface
    // Our viewDir points away from camera, so we need to negate it
    float dot_nv = max(dot(normal, -viewDir), 2e-5);
    float fresnel = fresnel_schlick(dot_nv, roughness);

    // Update roughness based on foam and fresnel (foam is matte, reflections are smooth)
    roughness = (1.0 - fresnel) * foamFactor + BASE_ROUGHNESS;

    // Calculate lighting components
    float dot_nl = max(dot(normal, sunWorldDir), 2e-5);

    // Halfway vector: In Godot, VIEW points toward camera, LIGHT points toward light
    // In our shader, viewDir points away from camera, sunWorldDir points toward sun
    // So we need to negate viewDir to match Godot's convention
    // Godot line 110: vec3 halfway = normalize(LIGHT + VIEW);
    vec3 halfway = normalize(sunWorldDir - viewDir);
    float dot_nh = max(dot(normal, halfway), 0.0);

    // --- SPECULAR (Cook-Torrance BRDF) ---
    // NOTE: Godot has a bug - they swap parameters to smith_masking_shadowing, passing (roughness, cos_theta)
    // We use the correct order: (cos_theta, roughness) per the function signature
    float light_mask = smith_masking_shadowing(dot_nv, roughness);
    float view_mask = smith_masking_shadowing(dot_nl, roughness);
    float microfacet_distribution = ggx_distribution(dot_nh, roughness);
    float geometric_attenuation = 1.0 / (1.0 + light_mask + view_mask);

    // ATTENUATION in Godot = shadow * sunVisibility (light availability)
    // This is the total light that reaches the surface
    float attenuation = shadow * sunVisibility;

    // Specular calculation matching Godot's formula (line 119)
    // SPECULAR_LIGHT += fresnel * microfacet_distribution * geometric_attenuation / (4.0 * dot_nv + 0.1) * ATTENUATION;
    float specularIntensity = fresnel * microfacet_distribution * geometric_attenuation / (4.0 * dot_nv + 0.1) * attenuation;

    // --- DIFFUSE (with Subsurface Scattering) ---
    const vec3 SSS_MODIFIER = vec3(0.9, 1.15, 0.85); // Green-shifted color for SSS

    // Subsurface scattering on wave peaks when backlit
    // Godot line 123: float sss_height = 1.0*max(0.0, wave_height + 2.5) * pow(max(dot(LIGHT, -VIEW), 0.0), 4.0) * ...
    // In Godot: LIGHT points to sun, VIEW points to camera, so -VIEW points away from camera (like our viewDir)
    // So we need: dot(sunWorldDir, viewDir)
    float sss_height = 1.0 * max(0.0, waveHeight + 2.5) *
                       pow(max(dot(sunWorldDir, viewDir), 0.0), 4.0) *
                       pow(0.5 - 0.5 * dot(sunWorldDir, normal), 3.0);

    // Near-surface subsurface scattering
    // Godot line 124: float sss_near = 0.5*pow(dot_nv, 2.0);
    // dot_nv is already calculated correctly above
    float sss_near = 0.5 * pow(dot_nv, 2.0);

    // Standard Lambertian diffuse
    float lambertian = 0.5 * dot_nl;

    // Combine diffuse components and blend with foam
    // This matches Godot line 126:
    // DIFFUSE_LIGHT += mix((sss_height + sss_near) * sss_modifier / (1.0 + light_mask) + lambertian, foam_color.rgb, foam_factor)
    //                  * (1.0 - fresnel) * ATTENUATION * LIGHT_COLOR;
    vec3 diffuse_color = mix(
        (sss_height + sss_near) * SSS_MODIFIER / (1.0 + light_mask) + lambertian,
        FOAM_COLOR,
        foamFactor
    );

    // Diffuse light contribution (note: sun color will be applied in final combination)
    vec3 diffuseLight = diffuse_color * (1.0 - fresnel) * attenuation;

    // ========================================================================
    // SCREEN-SPACE REFLECTIONS & REFRACTIONS (like original water shader)
    // ========================================================================

    vec2 screenCoordsOffset = normal.xy * REFL_BUMP;

#if @waterRefraction
    // Calculate water depth for proper refraction and shore blending
    float depthSample = linearizeDepth(sampleRefractionDepthMap(screenCoords), near, far);
    float surfaceDepth = linearizeDepth(gl_FragCoord.z, near, far);
    float realWaterDepth = depthSample - surfaceDepth;
    float depthSampleDistorted = linearizeDepth(sampleRefractionDepthMap(screenCoords - screenCoordsOffset * REFR_BUMP / REFL_BUMP), near, far);
    float waterDepthDistorted = max(depthSampleDistorted - surfaceDepth, 0.0);

    // Suppress normal-based distortion at shallow water (prevents shore artifacts)
    screenCoordsOffset *= clamp(realWaterDepth / BUMP_SUPPRESS_DEPTH, 0.0, 1.0);
#endif

    // Sample reflection with normal distortion
    vec3 reflection = sampleReflectionMap(screenCoords + screenCoordsOffset).rgb;

#if @waterRefraction
    // Sample refraction with normal distortion (opposite direction from reflection)
    vec3 refraction = sampleRefractionMap(screenCoords - screenCoordsOffset).rgb;

    // Apply underwater fog: blend refraction with water color based on depth
    // This makes deep water appear more blue/opaque
    if (cameraPos.z >= 0.0) // Above water
    {
        float depthCorrection = sqrt(1.0 + 4.0 * DEPTH_FADE * DEPTH_FADE);
        float factor = DEPTH_FADE * DEPTH_FADE / (-0.5 * depthCorrection + 0.5 - waterDepthDistorted / VISIBILITY) + 0.5 * depthCorrection + 0.5;
        refraction = mix(refraction, waterColorModulated, clamp(factor, 0.0, 1.0));
    }
    else // Underwater - brighten refraction
    {
        refraction = clamp(refraction * 1.5, 0.0, 1.0);
    }

    // Blend refraction and reflection based on Fresnel
    vec3 refrReflColor = mix(refraction, reflection, fresnel);
#else
    // No refraction available - blend water color with reflection
    vec3 refrReflColor = mix(waterColorModulated, reflection, (1.0 + fresnel) * 0.5);
#endif

    // --- AMBIENT ---
    // Base ambient from OpenMW's lighting system
    vec3 ambientLight = gl_LightModel.ambient.xyz * sunFade;

    // Add minimum ambient to prevent pure black at night
    // Increased from 0.15 to 0.25 to brighten ocean
    const float MIN_AMBIENT_STRENGTH = 0.25;
    ambientLight = max(ambientLight, vec3(MIN_AMBIENT_STRENGTH));

    // --- FINAL COLOR ---
    // Match Godot's rendering approach:
    // - Water surface = albedo × (ambient + diffuse × LIGHT_COLOR) + specular × LIGHT_COLOR + reflections
    // - Foam is more opaque, reduces reflection contribution

    // Base lighting from PBR (ambient + diffuse + specular)
    // This is the "opaque surface" component
    vec3 lighting = albedo * (ambientLight + diffuseLight * sunColor) + specularIntensity * sunColor;

    // Reflections/refractions provide the "transparent water" look
    // Reduce reflection intensity to avoid 100% mirror effect
    // Foam makes water more opaque (less reflective)
    float reflectionStrength = (1.0 - foamFactor * 0.5);

    // Distance-based reflection fade to hide reflection map seams/artifacts at horizon
    // Fade out reflections starting at 500m, completely gone by 1500m
    const float REFL_FADE_START = 500.0; // meters
    const float REFL_FADE_END = 1500.0;   // meters
    float reflectionDistanceFade = 1.0 - smoothstep(REFL_FADE_START, REFL_FADE_END, distToCamera_meters);
    reflectionStrength *= reflectionDistanceFade;

    // Combine: lighting + reflections (additive, not mix)
    // This gives proper water appearance: lit surface with reflections on top
    // Reduced from 0.5 to 0.35 to reduce visibility of reflection map artifacts
    vec3 finalColor = lighting + refrReflColor * reflectionStrength * 0.35;

    // Reduce alpha where there's foam (foam is more opaque)
    float alpha = mix(0.85, 0.95, foamFactor);

    // TEMPORARY DEBUG: Uncomment to test basic rendering
    //gl_FragData[0] = vec4(albedo, 1.0); applyShadowDebugOverlay(); return;
    //gl_FragData[0] = vec4(vec3(foam), 1.0); applyShadowDebugOverlay(); return; // Visualize foam
    //gl_FragData[0] = vec4(WATER_COLOR, 1.0); applyShadowDebugOverlay(); return; // Test water color
    //gl_FragData[0] = vec4(waterColorModulated, 1.0); applyShadowDebugOverlay(); return; // Test modulated water color
    //gl_FragData[0] = vec4(reflection, 1.0); applyShadowDebugOverlay(); return; // Visualize raw reflection
    //gl_FragData[0] = vec4(refrReflColor, 1.0); applyShadowDebugOverlay(); return; // Visualize combined refr/refl
    //gl_FragData[0] = vec4(lighting, 1.0); applyShadowDebugOverlay(); return; // Visualize PBR lighting only
    //gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0); applyShadowDebugOverlay(); return; // Visualize normals (should move with water)

    gl_FragData[0] = vec4(finalColor, alpha);

    // DEBUG: Uncomment one of these lines to visualize different components
    // gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0); // Solid Red (Geometry Check)
    // gl_FragData[0] = vec4(normal * 0.5 + 0.5, 1.0); // Normals
    // gl_FragData[0] = vec4(WATER_COLOR, 1.0); // Solid water color test
    // gl_FragData[0] = vec4(albedo, 1.0); // Albedo (water + foam mix)
    // gl_FragData[0] = vec4(vec3(shadow), 1.0); // Shadow value
    // gl_FragData[0] = vec4(sunColor, 1.0); // Sun color
    // gl_FragData[0] = vec4(gl_LightModel.ambient.xyz, 1.0); // Ambient light
    // gl_FragData[0] = vec4(ambient, 1.0); // Ambient term only
    // gl_FragData[0] = vec4(diffuse, 1.0); // Diffuse term only
    // gl_FragData[0] = vec4(specular, 1.0); // Specular term only
    // gl_FragData[0] = vec4(vec3(fresnel), 1.0); // Fresnel value
    // gl_FragData[0] = vec4(sunWorldDir * 0.5 + 0.5, 1.0); // Sun direction (RGB = XYZ)

    // DEBUG: Visualize distance to camera (gray gradient, darker = closer)
    // float distToCamera = length(worldPos.xy - cameraPosition.xy);
    // gl_FragData[0] = vec4(vec3(distToCamera / 50000.0), 1.0); return;

    // DEBUG: Visualize worldPos (should change as you move)
    // gl_FragData[0] = vec4(fract(worldPos.xyz * 0.0001), 1.0); return;

    // DEBUG: Visualize displacement magnitude from all cascades
    // vec3 totalDisp = vec3(0.0);
    // for (int i = 0; i < numCascades && i < 4; ++i) {
    //     vec2 uv = worldPos.xy * mapScales[i].x;
    //     vec3 disp = texture(displacementMap, vec3(uv, float(i))).xyz;
    //     totalDisp += disp * mapScales[i].z;
    // }
    // float dispMag = length(totalDisp);
    // gl_FragData[0] = vec4(vec3(dispMag * 0.01), 1.0); return; // Scale for visibility
    
    // Full rendering (currently disabled for testing)
    // gl_FragData[0] = colorWithFog;
    
    // DEBUG: Visualize Spectrum (Cascade 0)
    // vec4 spectrum = texture(spectrumMap, vec3(worldPos.xy * mapScales[0].x, 0.0));
    // gl_FragData[0] = vec4(spectrum.xyz, 1.0); // Show raw S, D, Amp
    
    // gl_FragData[0] = vec4(vec3(shadow), 1.0); // Shadow
    // gl_FragData[0] = vec4(vec3(linearDepth / 5000.0), 1.0); // Depth

    applyShadowDebugOverlay();
}
