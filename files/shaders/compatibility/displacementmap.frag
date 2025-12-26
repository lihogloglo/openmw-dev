#version 120

// Fragment shader for displacement map rendering
// Samples normal map alpha channel (displacement height) and weights by blend map
// Output is accumulated using additive blending

varying vec2 uv;

#if @normalMap
uniform sampler2D normalMap;
#endif

#if @blendMap
uniform sampler2D blendMap;
#endif

// Texture matrices for tiling
uniform mat4 textureMatrix0;  // For normal map tiling
uniform mat4 textureMatrix1;  // For blend map

void main()
{
    // Get height from normal map alpha, or default to 0.5 (neutral) if no normal map
    float height = 0.5;
#if @normalMap
    // Apply the same tiling as the terrain fragment shader
    // This ensures displacement aligns with the visual texture
    vec2 tiledUV = (textureMatrix0 * vec4(uv, 0.0, 1.0)).xy;
    height = texture2D(normalMap, tiledUV).a;
#endif

    // Get blend weight
    float blend = 1.0;
#if @blendMap
    vec2 blendUV = (textureMatrix1 * vec4(uv, 0.0, 1.0)).xy;
    blend = texture2D(blendMap, blendUV).a;
#endif

    // Output weighted height
    // We use a two-channel output:
    // R = weighted height sum (height * blend)
    // G = weight sum (blend)
    // The TES will compute final height as R/G
    gl_FragColor = vec4(height * blend, blend, 0.0, 1.0);
}
