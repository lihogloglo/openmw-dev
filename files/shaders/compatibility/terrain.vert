#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/vertex.h.glsl"

#if @snowDeformation
    #include "lib/terrain/deformation.glsl"

    uniform sampler2D deformationMap;
    uniform float deformationStrength;
    uniform vec2 textureCenter;
    uniform float worldTextureSize;

    // Sample deformation height from texture
    float sampleDeformation(vec2 worldPos)
    {
        vec2 offset = worldPos - textureCenter;
        vec2 uv = (offset / worldTextureSize) + 0.5;

        // Clamp to valid texture range
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            return 0.0;

        // Sample deformation texture (red channel contains height)
        return texture2D(deformationMap, uv).r;
    }

    // Calculate deformed normal using finite differences
    vec3 calculateDeformedNormal(vec2 worldPos, float centerHeight, vec3 originalNormal)
    {
        // Texel size in world units
        float delta = worldTextureSize / 1024.0;

        // Sample neighboring heights
        float heightRight = sampleDeformation(worldPos + vec2(delta, 0.0));
        float heightLeft = sampleDeformation(worldPos - vec2(delta, 0.0));
        float heightUp = sampleDeformation(worldPos + vec2(0.0, delta));
        float heightDown = sampleDeformation(worldPos - vec2(0.0, delta));

        // Calculate gradients
        float dx = (heightRight - heightLeft) / (2.0 * delta);
        float dy = (heightUp - heightDown) / (2.0 * delta);

        // Construct tangent space vectors
        vec3 tangent = normalize(vec3(1.0, 0.0, -dx * deformationStrength));
        vec3 bitangent = normalize(vec3(0.0, 1.0, -dy * deformationStrength));

        // Calculate deformed normal via cross product
        vec3 deformedNormal = normalize(cross(tangent, bitangent));

        // Blend with original normal for smoother transitions
        return normalize(mix(originalNormal, deformedNormal, min(centerHeight * 2.0, 1.0)));
    }
#endif

varying vec2 uv;
varying float euclideanDepth;
varying float linearDepth;

#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

#if !PER_PIXEL_LIGHTING
centroid varying vec3 passLighting;
centroid varying vec3 passSpecular;
centroid varying vec3 shadowDiffuseLighting;
centroid varying vec3 shadowSpecularLighting;
#endif
varying vec3 passViewPos;
varying vec3 passNormal;

#include "vertexcolors.glsl"
#include "shadows_vertex.glsl"
#include "compatibility/normals.glsl"

#include "lib/light/lighting.glsl"
#include "lib/view/depth.glsl"

void main(void)
{
    vec4 vertex = gl_Vertex;
    vec3 normal = gl_Normal;

#if @snowDeformation
    // Get world position of vertex (gl_ModelViewMatrix includes world transform for terrain chunks)
    vec4 worldPos4 = gl_ModelViewMatrix * gl_Vertex;
    vec2 worldPosXY = worldPos4.xy;

    // Sample deformation at this world position
    float deformation = sampleDeformation(worldPosXY);

    // Apply deformation if significant
    if (deformation > 0.01) {
        // Apply vertical displacement (negative = sink into terrain)
        float depthMultiplier = getDepthMultiplier(1); // TERRAIN_SNOW
        vertex.z -= deformation * deformationStrength * depthMultiplier;

        // Recalculate normal from deformation gradient
        normal = calculateDeformedNormal(worldPosXY, deformation, gl_Normal);
    }
#endif

    gl_Position = modelToClip(vertex);

    vec4 viewPos = modelToView(vertex);
    gl_ClipVertex = viewPos;
    euclideanDepth = length(viewPos.xyz);
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);

    passColor = gl_Color;
    passNormal = normal;
    passViewPos = viewPos.xyz;
    normalToViewMatrix = gl_NormalMatrix;

#if @normalMap
    mat3 tbnMatrix = generateTangentSpace(vec4(1.0, 0.0, 0.0, -1.0), passNormal);
    tbnMatrix[0] = -normalize(cross(tbnMatrix[2], tbnMatrix[1])); // our original tangent was not at a 90 degree angle to the normal, so we need to rederive it
    normalToViewMatrix *= tbnMatrix;
#endif

#if !PER_PIXEL_LIGHTING || @shadows_enabled
    vec3 viewNormal = normalize(gl_NormalMatrix * passNormal);
#endif

#if !PER_PIXEL_LIGHTING
    vec3 diffuseLight, ambientLight, specularLight;
    doLighting(viewPos.xyz, viewNormal, gl_FrontMaterial.shininess, diffuseLight, ambientLight, specularLight, shadowDiffuseLighting, shadowSpecularLighting);
    passLighting = getDiffuseColor().xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
    passSpecular = getSpecularColor().xyz * specularLight;
    clampLightingResult(passLighting);
    shadowDiffuseLighting *= getDiffuseColor().xyz;
    shadowSpecularLighting *= getSpecularColor().xyz;
#endif

    uv = gl_MultiTexCoord0.xy;

#if (@shadows_enabled)
    setupShadowCoords(viewPos, viewNormal);
#endif
}
