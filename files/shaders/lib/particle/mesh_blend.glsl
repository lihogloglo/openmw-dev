#ifndef LIB_PARTICLE_MESH_BLEND
#define LIB_PARTICLE_MESH_BLEND

#include "lib/util/quickstep.glsl"

float meshBlendViewDepth(float depth, float near, float far)
{
#if @reverseZ
    depth = 1.0 - depth;
#endif
    return (near * far) / ((far - near) * depth - far);
}

float calcMeshBlendFade(
    in vec3 viewPos,
    float near,
    float far,
    float depth,
    float size
    )
{
    const float falloffMultiplier = 0.33;
    const float contrast = 1.30;

    float sceneDepth = meshBlendViewDepth(depth, near, far);
    float particleDepth = viewPos.z;
    float falloff = size * falloffMultiplier;
    float delta = particleDepth - sceneDepth;

    // No view bias, no shift (max opacity 1.0)
    return pow(clamp(delta/falloff, 0.0, 1.0), contrast);
}

#endif
