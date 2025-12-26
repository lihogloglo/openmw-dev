#ifndef COMPONENTS_TERRAIN_MATERIAL_H
#define COMPONENTS_TERRAIN_MATERIAL_H

#include <osg/StateSet>

namespace osg
{
    class Texture2D;
}

namespace Resource
{
    class SceneManager;
}

namespace Terrain
{

    class TextureManager;

    struct LayerInfo;

    struct TextureLayer
    {
        osg::ref_ptr<osg::Texture2D> mDiffuseMap;
        osg::ref_ptr<osg::Texture2D> mNormalMap; // optional
        bool mParallax = false;
        bool mSpecular = false;
    };

    std::vector<osg::ref_ptr<osg::StateSet>> createPasses(bool useShaders, Resource::SceneManager* sceneManager,
        const std::vector<TextureLayer>& layers, const std::vector<osg::ref_ptr<osg::Texture2D>>& blendmaps,
        int blendmapScale, float layerTileSize, bool esm4terrain = false);

    /// Create passes using tessellation shaders (requires GL 4.0+)
    /// Returns empty vector if tessellation shaders fail to load
    std::vector<osg::ref_ptr<osg::StateSet>> createTessellationPasses(Resource::SceneManager* sceneManager,
        const std::vector<TextureLayer>& layers, const std::vector<osg::ref_ptr<osg::Texture2D>>& blendmaps,
        int blendmapScale, float layerTileSize, bool esm4terrain = false);

    /// Create passes for rendering the blended displacement map
    /// Each pass renders one layer's height contribution weighted by its blend map
    /// @param chunkSize Size of the chunk in cells
    /// @param chunkCenter Center position of the chunk in cell coordinates
    std::vector<osg::ref_ptr<osg::StateSet>> createDisplacementMapPasses(Resource::SceneManager* sceneManager,
        const std::vector<LayerInfo>& layers, const std::vector<osg::ref_ptr<osg::Texture2D>>& blendmaps,
        float layerTileSize, float chunkSize, const osg::Vec2f& chunkCenter, TextureManager* textureManager);
}

#endif
