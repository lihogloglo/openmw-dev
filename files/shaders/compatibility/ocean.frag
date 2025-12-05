#version 330 compatibility

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

// Include fragment.h.glsl which provides:
// - sampleReflectionMap(vec2 uv)
// - sampleRefractionMap(vec2 uv) (if @waterRefraction)
// - sampleRefractionDepthMap(vec2 uv) (if @waterRefraction)
// These link to lib/core/fragment.glsl which defines the uniforms and implementations
#include "lib/core/fragment.h.glsl"

// Ocean water fragment shader - FFT-based water rendering
// Based on GodotOceanWaves

// Water rendering constants (matching original water shader)
const float VISIBILITY = 2500.0;
const float VISIBILITY_DEPTH = VISIBILITY * 1.5;
const float DEPTH_FADE = 0.15;
// Water and foam colors - now runtime configurable via uniforms
// Default: waterColor = vec3(0.15, 0.25, 0.35), foamColor = vec3(1.0, 1.0, 1.0)
uniform vec3 waterColor;
uniform vec3 foamColor;

// Shore smoothing parameters (runtime configurable via Lua)
// Controls how waves calm down near shores based on water depth
uniform float shoreWaveAttenuation;  // 0.0 = no attenuation, 1.0 = full attenuation at shore (default: 0.8)
uniform float shoreDepthScale;       // Depth at which waves reach full amplitude in MW units (default: 500.0)
uniform float shoreFoamBoost;        // Extra foam near shores (default: 1.5)

// Shore distance map - automatic shore detection based on terrain (shared with vertex shader)
// Format: R16F texture where 0 = on shore, 1 = far from shore (open ocean)
uniform sampler2D shoreDistanceMap;
uniform vec4 shoreMapBounds;     // vec4(minX, minY, maxX, maxY) in world coords
uniform int hasShoreDistanceMap; // 0 = disabled, 1 = enabled

// Sunlight scattering constants (from original water shader)
#if @sunlightScattering
const float SCATTER_AMOUNT = 0.3;                  // amount of sunlight scattering
const vec3 SCATTER_COLOUR = vec3(0.0, 1.0, 0.95);  // colour of sunlight scattering (cyan-ish)
const vec3 SUN_EXT = vec3(0.45, 0.55, 0.68);       // sunlight extinction
#endif

// Reflection/refraction distortion
const float REFL_BUMP = 0.20;  // reflection distortion amount (increased to hide reflection map seams)
const float REFR_BUMP = 0.12;  // refraction distortion amount (increased from 0.07)
const float BUMP_SUPPRESS_DEPTH = 300.0; // suppress distortion at shores

// SSR (Screen-Space Reflections) mode - alternative to RTT reflections
// When enabled, uses raymarching in screen space with cubemap fallback
#if @useSSR
uniform sampler2D sceneColorBuffer;  // Full scene color for SSR sampling
uniform samplerCube environmentMap;  // Cubemap fallback for SSR misses
uniform float ssrMixStrength;        // Blend factor between SSR and cubemap (0-1)
#include "lib/water/ssr.glsl"
#endif

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
uniform int debugVisualizeShore;    // 0 = off, 1 = show shore depth factor

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

// Fresnel-Schlick approximation (modified for water) - kept for PBR specular
float fresnel_schlick(float cos_theta, float roughness) {
    return mix(
        pow(1.0 - cos_theta, 5.0 * exp(-2.69 * roughness)) / (1.0 + 22.7 * pow(roughness, 1.5)),
        1.0,
        REFLECTANCE
    );
}

// Specular constants (from original water shader)
const float SPEC_HARDNESS = 256.0;    // specular highlights hardness
const float SPEC_BUMPINESS = 5.0;     // surface bumpiness boost for specular
const float SPEC_BRIGHTNESS = 1.5;    // boosts the brightness of the specular highlights
const float SPEC_MAGIC = 1.55;        // from the original blender shader

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

    // ========================================================================
    // SHORE SMOOTHING - Unified system using both depth buffer and distance map
    // ========================================================================
    // Both vertex displacement (big waves) and fragment normals (small details)
    // use the same shore distance map for consistent wave attenuation near shores
    float shoreDepthFactor = 1.0; // 1.0 = full waves, 0.0 = calm water
    float shoreMapFactor = 1.0;   // From shore distance map: 0 = shore, 1 = open ocean
    float depthBufferFactor = 1.0; // From depth buffer: 0 = shore, 1 = deep water

    // Sample shore distance map (same as vertex shader)
    vec2 worldPosXY_early = worldPos.xy;
    if (hasShoreDistanceMap == 1)
    {
        vec2 shoreUV = (worldPosXY_early - shoreMapBounds.xy) / (shoreMapBounds.zw - shoreMapBounds.xy);
        if (shoreUV.x >= 0.0 && shoreUV.x <= 1.0 && shoreUV.y >= 0.0 && shoreUV.y <= 1.0)
        {
            // shoreMapFactor: 0 = on shore, 1 = far from shore (open ocean)
            shoreMapFactor = texture(shoreDistanceMap, shoreUV).r;
        }
    }

#if @waterRefraction
    // Sample depth at screen center (no distortion yet) for shore detection
    float earlyDepthSample = linearizeDepth(sampleRefractionDepthMap(screenCoords), near, far);
    float earlySurfaceDepth = linearizeDepth(gl_FragCoord.z, near, far);
    float earlyWaterDepth = max(earlyDepthSample - earlySurfaceDepth, 0.0);

    // Calculate depth buffer factor: 0 at shore, 1 in deep water
    depthBufferFactor = smoothstep(0.0, shoreDepthScale, earlyWaterDepth);
#endif

    // Combine both factors - use the minimum (most conservative) to ensure
    // waves are calmed when EITHER system detects shore proximity
    shoreDepthFactor = min(shoreMapFactor, depthBufferFactor);

    // Apply attenuation control: blend between full waves and calmed waves
    // shoreWaveAttenuation controls how much waves are reduced at shore (0 = no effect, 1 = full calm)
    shoreDepthFactor = mix(1.0, shoreDepthFactor, shoreWaveAttenuation);

    // Debug: Visualize shore factors
    if (debugVisualizeShore == 1) {
        // Red channel: shore map factor (0 = shore from distance map)
        // Green channel: combined factor (0 = calmed, 1 = full waves)
        // Blue channel: depth buffer factor (0 = shallow from depth)
        vec3 debugColor = vec3(1.0 - shoreMapFactor, shoreDepthFactor, depthBufferFactor);
        gl_FragData[0] = vec4(debugColor, 1.0);
        return;
    }

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

    // ========================================================================
    // CASCADE-BASED SHORE SMOOTHING
    // ========================================================================
    // Near shores (shallow water), we suppress large wave cascades but keep small ripples
    // This creates realistic wave behavior where big swells break/calm at coast
    // Cascade weights based on shore depth:
    //   - Cascade 0 (large swells): fully suppressed near shore
    //   - Cascade 1 (medium waves): partially suppressed
    //   - Cascade 2-3 (small ripples): kept for surface detail
    float cascadeShoreWeights[4];
    cascadeShoreWeights[0] = shoreDepthFactor;                              // Large waves: full suppression
    cascadeShoreWeights[1] = mix(0.3, 1.0, shoreDepthFactor);               // Medium: partial suppression
    cascadeShoreWeights[2] = mix(0.6, 1.0, shoreDepthFactor);               // Small ripples: mostly kept
    cascadeShoreWeights[3] = mix(0.8, 1.0, shoreDepthFactor);               // Fine detail: almost full

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

        // Apply per-cascade shore weight to reduce waves near shore
        float shoreWeight = cascadeShoreWeights[i];

        // Apply per-cascade normal scale to gradient (with shore attenuation)
        // Godot line 83: gradient += ... * vec3(scales.ww, 1.0);
        // scales.w is the normal scale, which is mapScales[i].w for us
        gradient.xy += normalSample.xy * mapScales[i].w * shoreWeight;
        foam += normalSample.w * shoreWeight;
    }

    // Boost foam near shores (breaking waves effect)
#if @waterRefraction
    float shoreProximity = 1.0 - shoreDepthFactor;
    foam += shoreProximity * shoreFoamBoost * 0.5; // Extra foam at shore
#endif

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
    // Using runtime configurable foam color (default: white)
    float foamFactor = smoothstep(0.0, 1.0, foam);
    vec3 waterColorModulated = waterColor * sunFade;
    vec3 albedo = mix(waterColorModulated, foamColor, foamFactor);

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

    // Calculate Fresnel using physically accurate IOR-based calculation (like original water shader)
    // This properly handles air-to-water and water-to-air transitions
    float ior = (cameraPos.z > 0.0) ? (1.333 / 1.0) : (1.0 / 1.333);
    float fresnel = clamp(fresnel_dielectric(viewDir, normal, ior), 0.0, 1.0);

    // Also calculate dot_nv for PBR lighting calculations
    float dot_nv = max(dot(normal, -viewDir), 2e-5);
    float fresnelPBR = fresnel_schlick(dot_nv, roughness);

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

    // --- SPECULAR (Cook-Torrance BRDF for subsurface/diffuse, Phong for sun specular) ---
    float light_mask = smith_masking_shadowing(dot_nv, roughness);
    float view_mask = smith_masking_shadowing(dot_nl, roughness);
    float microfacet_distribution = ggx_distribution(dot_nh, roughness);
    float geometric_attenuation = 1.0 / (1.0 + light_mask + view_mask);

    // ATTENUATION = shadow * sunVisibility (light availability)
    float attenuation = shadow * sunVisibility;

    // PBR specular for subsurface calculations (using fresnelPBR)
    float specularPBR = fresnelPBR * microfacet_distribution * geometric_attenuation / (4.0 * dot_nv + 0.1) * attenuation;

    // Classic Phong specular for sun highlights (like original water shader)
    // This gives sharper, more visually pleasing sun reflections
    // Note: Our normal is (X, Y-up, Z) but specular formula expects (X, Y, Z-up)
    // So we use normal.xz for horizontal perturbation and normal.y for vertical
    //
    // FFT normals already have lots of detail, so we use lower bumpiness than original water
    // Also apply distance falloff to reduce specular noise at distance
    float specBumpiness = SPEC_BUMPINESS * 0.3 * normalFalloff; // Reduced from 5.0 to ~1.5, with distance fade
    vec3 specNormal = normalize(vec3(normal.x * specBumpiness, normal.z * specBumpiness, normal.y));
    vec3 viewReflectDir = reflect(viewDir, specNormal);
    float phongTerm = max(dot(viewReflectDir, sunWorldDir), 0.0);
    float specular = pow(atan(phongTerm * SPEC_MAGIC), SPEC_HARDNESS) * SPEC_BRIGHTNESS;
    specular = clamp(specular, 0.0, 1.0) * shadow * sunVisibility;

    // --- DIFFUSE (with Subsurface Scattering) ---
    // Blue-green color for SSS to match Godot's brighter wave crests
    // Increased green and blue channels for more visible SSS effect
    const vec3 SSS_MODIFIER = vec3(0.8, 1.5, 1.3);

    // Subsurface scattering on wave peaks when backlit
    // Increased multiplier from 1.0 to 2.0 for more visible SSS on wave crests
    // Godot line 123: float sss_height = 1.0*max(0.0, wave_height + 2.5) * pow(max(dot(LIGHT, -VIEW), 0.0), 4.0) * ...
    // In Godot: LIGHT points to sun, VIEW points to camera, so -VIEW points away from camera (like our viewDir)
    // So we need: dot(sunWorldDir, viewDir)
    float sss_height = 1.0 * max(0.0, waveHeight + 2.5) *
                       pow(max(dot(sunWorldDir, viewDir), 0.0), 4.0) *
                       pow(0.5 - 0.5 * dot(sunWorldDir, normal), 3.0);

    // Near-surface subsurface scattering
    // Increased from 0.5 to 1.0 for more ambient SSS glow
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
        foamColor,
        foamFactor
    );

    // Diffuse light contribution (using fresnelPBR for energy conservation)
    vec3 diffuseLight = diffuse_color * (1.0 - fresnelPBR) * attenuation * sunSpec.rgb;

    // ========================================================================
    // SCREEN-SPACE REFLECTIONS & REFRACTIONS (like original water shader)
    // ========================================================================

    // Note: Our normal is (X, Y-up, Z) so use xz for horizontal screen distortion
    vec2 screenCoordsOffset = normal.xz * REFL_BUMP;

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

    // Sample reflection - either via SSR+cubemap or traditional RTT
#if @useSSR
    // ========================================================================
    // SSR + CUBEMAP REFLECTION PATH (Performance mode)
    // ========================================================================
    // Calculate view-space position and normal for SSR raymarching
    vec3 viewPos = (gl_ModelViewMatrix * vec4(worldPos, 1.0)).xyz;
    vec3 viewNormal = normalize(gl_NormalMatrix * normal);

    // Trace SSR - use opaqueDepthTex which is always available from fragment.glsl
    bool ssrReverseZ = false;
#if @reverseZ
    ssrReverseZ = true;
#endif
    vec4 ssrResult = traceSSRSimple(viewPos, viewNormal, sceneColorBuffer,
                                    opaqueDepthTex, near, far, ssrReverseZ);

    // Sample cubemap fallback using world-space reflection direction
    vec3 worldReflectDir = reflect(viewDir, normal);
    // Swap Y and Z for cubemap (cubemaps use Y-up convention)
    vec3 cubemapDir = vec3(worldReflectDir.x, worldReflectDir.z, -worldReflectDir.y);
    vec3 cubemapReflection = texture(environmentMap, cubemapDir).rgb;

    // Blend SSR with cubemap based on SSR confidence and mix strength
    float ssrConfidence = ssrResult.a * ssrMixStrength;
    vec3 reflection = mix(cubemapReflection, ssrResult.rgb, ssrConfidence);

    // Apply normal-based distortion to reflection (subtle, since SSR already has it)
    // This helps blend the SSR with cubemap at edges
    reflection = mix(reflection, cubemapReflection, (1.0 - ssrScreenEdgeFade(screenCoords)) * 0.5);
#else
    // ========================================================================
    // RTT REFLECTION PATH (Quality mode - traditional planar reflections)
    // ========================================================================
    vec3 reflection = sampleReflectionMap(screenCoords + screenCoordsOffset).rgb;
#endif

#if @waterRefraction
    // Sample refraction with normal distortion (opposite direction from reflection)
    vec3 refraction = sampleRefractionMap(screenCoords - screenCoordsOffset).rgb;

    // DEBUG: Output red to confirm shader runs up to this point
    // gl_FragData[0] = vec4(1.0, 0.0, 0.0, 1.0); return;

    // DEBUG: Output refraction to see if it's valid before absorption
    // gl_FragData[0] = vec4(refraction, 1.0); return;

    // DEBUG: Check what cameraPos.z contains
    // gl_FragData[0] = vec4(cameraPos.z > 0.0 ? 1.0 : 0.0, 0.0, 0.0, 1.0); return;

    // DEBUG: Check waterDepthDistorted
    // gl_FragData[0] = vec4(vec3(waterDepthDistorted / 1000.0), 1.0); return;

    // Apply depth-based absorption (underwater fog effect)
    // This makes deep water appear more opaque with the water color
    if (cameraPos.z > 0.0) // Above water
    {
        // Use the same absorption formula as the original water shader
        // This creates a smooth falloff from clear shallow water to opaque deep water
        float depthCorrection = sqrt(1.0 + 4.0 * DEPTH_FADE * DEPTH_FADE);
        float factor = DEPTH_FADE * DEPTH_FADE / (-0.5 * depthCorrection + 0.5 - waterDepthDistorted / VISIBILITY) + 0.5 * depthCorrection + 0.5;
        refraction = mix(refraction, waterColorModulated, clamp(factor, 0.0, 1.0));
    }
    else // Underwater - brighten refraction
    {
        refraction = clamp(refraction * 1.5, 0.0, 1.0);
    }

    // DEBUG: Output refraction AFTER absorption to see if it's still valid
    // gl_FragData[0] = vec4(refraction, 1.0); return;

#if @sunlightScattering
    // Sunlight scattering effect - adds vibrant cyan-ish glow when looking toward sun through water
    // Uses a simplified normal for scattering (less bumpy than surface normal)
    vec3 scatterNormal = normalize(vec3(-gradient.x * 0.5, 1.0, -gradient.y * 0.5));
    float sunHeight = sunWorldDir.z;

    // Scatter color changes based on sun height (more orange near horizon)
    vec3 scatterColour = mix(SCATTER_COLOUR * vec3(1.0, 0.4, 0.0), SCATTER_COLOUR, max(1.0 - exp(-sunHeight * SUN_EXT), 0.0));

    // Lambert-like term for scatter intensity
    float scatterLambert = max(dot(sunWorldDir, scatterNormal) * 0.7 + 0.3, 0.0);

    // Backscatter when looking toward sun through water
    float scatterReflectAngle = max(dot(reflect(sunWorldDir, scatterNormal), viewDir) * 2.0 - 1.2, 0.0);

    // Combine scatter factors with sun visibility
    float lightScatter = scatterLambert * scatterReflectAngle * SCATTER_AMOUNT * sunFade * sunVisibility * max(1.0 - exp(-sunHeight), 0.0);

    // Apply scattering to refraction
    refraction = mix(refraction, scatterColour, lightScatter);
#endif

    // Blend refraction and reflection based on Fresnel
    vec3 refrReflColor = mix(refraction, reflection, fresnel);
#else
    // No refraction available - blend water color with reflection
    vec3 refrReflColor = mix(waterColorModulated, reflection, (1.0 + fresnel) * 0.5);
#endif

    // --- FINAL COLOR COMPOSITION ---
    // Start with reflection/refraction blend (like original water shader line 216)
    gl_FragData[0].rgb = refrReflColor;
    gl_FragData[0].a = 1.0;

    // Add sun specular highlights (like original water shader line 225)
    gl_FragData[0].rgb += specular * sunSpec.rgb;

    // Apply fog
    // Note: Use worldPos instead of position.xyz - our vertex shader sets position to gl_Vertex (local)
    // but worldPos contains the actual displaced world position
#if @radialFog
    float radialDepth = distance(worldPos, cameraPos);
#else
    float radialDepth = 0.0;
#endif

    // DEBUG: skip fog entirely to test
    // gl_FragData[0] = applyFogAtDist(gl_FragData[0], radialDepth, linearDepth, far);

#if !@disableNormals
    gl_FragData[1].rgb = normalize(gl_NormalMatrix * normal) * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
