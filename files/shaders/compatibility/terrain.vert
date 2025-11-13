#if @terrainDeformTess
    #version 400 compatibility

    #if @useUBO
        #extension GL_ARB_uniform_buffer_object : require
    #endif

    #if @useGPUShader4
        #extension GL_EXT_gpu_shader4: require
    #endif

    // For tessellation, define vertex transformation functions inline to avoid #version conflicts
    uniform mat4 projectionMatrix;

    vec4 modelToClip(vec4 pos)
    {
        return projectionMatrix * gl_ModelViewMatrix * pos;
    }

    vec4 modelToView(vec4 pos)
    {
        return gl_ModelViewMatrix * pos;
    }

    vec4 viewToClip(vec4 pos)
    {
        return projectionMatrix * pos;
    }
#else
    #version 120

    #if @useUBO
        #extension GL_ARB_uniform_buffer_object : require
    #endif

    #if @useGPUShader4
        #extension GL_EXT_gpu_shader4: require
    #endif

    #include "lib/core/vertex.h.glsl"
#endif

#define PER_PIXEL_LIGHTING (@normalMap || @specularMap || @forcePPL)

#if !@terrainDeformTess
// Non-tessellation path: output directly to fragment shader
varying vec2 uv;
varying float euclideanDepth;
varying float linearDepth;

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
#else
// Tessellation path: outputs go to TCS, not fragment shader
// Define passColor inline for GLSL 400 compatibility (no varying keyword)
centroid out vec4 passColor;

uniform int colorMode;

const int ColorMode_None = 0;
const int ColorMode_Emission = 1;
const int ColorMode_AmbientAndDiffuse = 2;
const int ColorMode_Ambient = 3;
const int ColorMode_Diffuse = 4;
const int ColorMode_Specular = 5;

vec4 getEmissionColor()
{
    if (colorMode == ColorMode_Emission)
        return passColor;
    return gl_FrontMaterial.emission;
}

vec4 getAmbientColor()
{
    if (colorMode == ColorMode_AmbientAndDiffuse || colorMode == ColorMode_Ambient)
        return passColor;
    return gl_FrontMaterial.ambient;
}

vec4 getDiffuseColor()
{
    if (colorMode == ColorMode_AmbientAndDiffuse || colorMode == ColorMode_Diffuse)
        return passColor;
    return gl_FrontMaterial.diffuse;
}

vec4 getSpecularColor()
{
    if (colorMode == ColorMode_Specular)
        return passColor;
    return gl_FrontMaterial.specular;
}

// Shadow coordinate outputs for tessellation (no varying keyword)
#if @shadows_enabled
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        uniform mat4 shadowSpaceMatrix@shadow_texture_unit_index;
        out vec4 shadowSpaceCoords@shadow_texture_unit_index;

#if @perspectiveShadowMaps
        uniform mat4 validRegionMatrix@shadow_texture_unit_index;
        out vec4 shadowRegionCoords@shadow_texture_unit_index;
#endif
    @endforeach
    const bool onlyNormalOffsetUV = false;

void setupShadowCoords(vec4 viewPos, vec3 viewNormal)
{
    vec4 shadowOffset;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
#if @perspectiveShadowMaps
        shadowRegionCoords@shadow_texture_unit_index = validRegionMatrix@shadow_texture_unit_index * viewPos;
#endif

#if @disableNormalOffsetShadows
        shadowSpaceCoords@shadow_texture_unit_index = shadowSpaceMatrix@shadow_texture_unit_index * viewPos;
#else
        shadowOffset = vec4(viewNormal * @shadowNormalOffset, 0.0);

        if (onlyNormalOffsetUV)
        {
            vec4 lightSpaceXY = viewPos + shadowOffset;
            lightSpaceXY = shadowSpaceMatrix@shadow_texture_unit_index * lightSpaceXY;

            shadowSpaceCoords@shadow_texture_unit_index.xy = lightSpaceXY.xy;
        }
        else
        {
            vec4 offsetViewPosition = viewPos + shadowOffset;
            shadowSpaceCoords@shadow_texture_unit_index = shadowSpaceMatrix@shadow_texture_unit_index * offsetViewPosition;
        }
#endif
    @endforeach
}
#else
void setupShadowCoords(vec4 viewPos, vec3 viewNormal)
{
    // No shadows
}
#endif
#endif

#if !@terrainDeformTess
#include "lib/light/lighting.glsl"
#include "lib/view/depth.glsl"
#else
// Inline depth functions for tessellation to avoid #version conflicts
#ifndef LIB_VIEW_DEPTH
#define LIB_VIEW_DEPTH

float linearizeDepth(float depth, float near, float far)
{
#if @reverseZ
    depth = 1.0 - depth;
#endif
    float z_n = 2.0 * depth - 1.0;
    depth = 2.0 * near * far / (far + near - z_n * (far - near));
    return depth;
}

float getLinearDepth(in float z, in float viewZ)
{
#if @reverseZ
    return -viewZ;
#else
    return z;
#endif
}

#endif
// For tessellation, we'll define lighting inline later if needed
#endif

#if @terrainDeformTess
// Tessellation outputs
out vec3 worldPos_TC_in;
out vec2 uv_TC_in;
out vec3 passNormal_TC_in;
out vec3 passViewPos_TC_in;

#if !PER_PIXEL_LIGHTING
out vec3 passLighting_TC_in;
out vec3 passSpecular_TC_in;
out vec3 shadowDiffuseLighting_TC_in;
out vec3 shadowSpecularLighting_TC_in;
#endif

out vec4 passColor_TC_in;
out float euclideanDepth_TC_in;
out float linearDepth_TC_in;
#endif

void main(void)
{
    gl_Position = modelToClip(gl_Vertex);

    vec4 viewPos = modelToView(gl_Vertex);
    gl_ClipVertex = viewPos;

    // Declare viewNormal early for shadow setup (needed in both paths)
    vec3 viewNormal;

#if !@terrainDeformTess
    // Direct outputs for non-tessellation path
    euclideanDepth = length(viewPos.xyz);
    linearDepth = getLinearDepth(gl_Position.z, viewPos.z);
    passColor = gl_Color;
    passNormal = gl_Normal.xyz;
    passViewPos = viewPos.xyz;
    normalToViewMatrix = gl_NormalMatrix;
#else
    // Temporary variables for tessellation path
    float tempEuclideanDepth = length(viewPos.xyz);
    float tempLinearDepth = getLinearDepth(gl_Position.z, viewPos.z);
    vec4 tempPassColor = gl_Color;
    vec3 tempPassNormal = gl_Normal.xyz;
    vec3 tempPassViewPos = viewPos.xyz;
#endif

#if !@terrainDeformTess
    // Normal mapping setup for non-tessellation path
    #if @normalMap
        mat3 tbnMatrix = generateTangentSpace(vec4(1.0, 0.0, 0.0, -1.0), passNormal);
        tbnMatrix[0] = -normalize(cross(tbnMatrix[2], tbnMatrix[1]));
        normalToViewMatrix *= tbnMatrix;
    #endif

    #if !PER_PIXEL_LIGHTING || @shadows_enabled
        viewNormal = normalize(gl_NormalMatrix * passNormal);
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
#else
    // Lighting for tessellation path
    viewNormal = normalize(gl_NormalMatrix * tempPassNormal);

    #if !PER_PIXEL_LIGHTING
        vec3 tempPassLighting, tempPassSpecular;
        vec3 tempShadowDiffuseLighting, tempShadowSpecularLighting;
        vec3 diffuseLight, ambientLight, specularLight;
        doLighting(viewPos.xyz, viewNormal, gl_FrontMaterial.shininess, diffuseLight, ambientLight, specularLight, tempShadowDiffuseLighting, tempShadowSpecularLighting);
        tempPassLighting = getDiffuseColor().xyz * diffuseLight + getAmbientColor().xyz * ambientLight + getEmissionColor().xyz;
        tempPassSpecular = getSpecularColor().xyz * specularLight;
        clampLightingResult(tempPassLighting);
        tempShadowDiffuseLighting *= getDiffuseColor().xyz;
        tempShadowSpecularLighting *= getSpecularColor().xyz;
    #endif

    vec2 tempUv = gl_MultiTexCoord0.xy;
#endif

#if (@shadows_enabled)
    setupShadowCoords(viewPos, viewNormal);
#endif

#if @terrainDeformTess
    // Pass data to tessellation control shader
    // Calculate world position using inverse of view matrix
    worldPos_TC_in = (gl_ModelViewMatrixInverse * viewPos).xyz;
    uv_TC_in = tempUv;
    passNormal_TC_in = tempPassNormal;
    passViewPos_TC_in = tempPassViewPos;

#if !PER_PIXEL_LIGHTING
    passLighting_TC_in = tempPassLighting;
    passSpecular_TC_in = tempPassSpecular;
    shadowDiffuseLighting_TC_in = tempShadowDiffuseLighting;
    shadowSpecularLighting_TC_in = tempShadowSpecularLighting;
#endif

    passColor_TC_in = tempPassColor;
    euclideanDepth_TC_in = tempEuclideanDepth;
    linearDepth_TC_in = tempLinearDepth;
#endif
}
