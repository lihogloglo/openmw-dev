#include "terrainweights.hpp"
#include "storage.hpp"
#include "defs.hpp"
#include "snowdetection.hpp"

#include <components/debug/debuglog.hpp>

#include <algorithm>
#include <cmath>

namespace Terrain
{
    const osg::Vec4f TerrainWeights::DEFAULT_ROCK_WEIGHT = osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f);

    TerrainWeights::WeightLOD TerrainWeights::determineLOD(float distanceToPlayer)
    {
        if (distanceToPlayer < LOD_FULL_DISTANCE)
            return LOD_FULL;
        else if (distanceToPlayer < LOD_SIMPLIFIED_DISTANCE)
            return LOD_SIMPLIFIED;
        else
            return LOD_NONE;
    }

    osg::ref_ptr<osg::Vec4Array> TerrainWeights::computeWeights(
        const osg::Vec3Array* vertices,
        const osg::Vec2f& chunkCenter,
        float chunkSize,
        const std::vector<LayerInfo>& layerList,
        const std::vector<osg::ref_ptr<osg::Image>>& blendmaps,
        Storage* terrainStorage,
        ESM::RefId worldspace,
        const osg::Vec3f& playerPosition,
        float cellWorldSize,
        WeightLOD lod)
    {
        if (!vertices || vertices->empty())
            return nullptr;

        osg::ref_ptr<osg::Vec4Array> weights = new osg::Vec4Array;
        weights->reserve(vertices->size());

        // LOD optimization: For far terrain, skip computation entirely
        if (lod == LOD_NONE)
        {
            // All vertices get rock weight (no deformation)
            for (size_t i = 0; i < vertices->size(); ++i)
                weights->push_back(DEFAULT_ROCK_WEIGHT);

            return weights;
        }

        // LOD optimization: For medium distance, compute once and reuse
        if (lod == LOD_SIMPLIFIED)
        {
            // Sample at chunk center only
            osg::Vec3f centerVertex(0.0f, 0.0f, 0.0f); // Chunk center in local coords
            osg::Vec4f chunkWeight = computeVertexWeightDirect(
                centerVertex, chunkCenter, terrainStorage, worldspace, cellWorldSize);

            // Apply same weight to all vertices
            for (size_t i = 0; i < vertices->size(); ++i)
                weights->push_back(chunkWeight);

            return weights;
        }

        // LOD_FULL: Compute per-vertex weights using direct land data sampling
        // This ensures chunk boundary consistency - vertices at the same world position
        // always get the same texture and weight, regardless of which chunk they belong to
        for (size_t i = 0; i < vertices->size(); ++i)
        {
            const osg::Vec3f& vertexPos = (*vertices)[i];
            osg::Vec4f weight = computeVertexWeightDirect(
                vertexPos, chunkCenter, terrainStorage, worldspace, cellWorldSize);
            weights->push_back(weight);
        }

        return weights;
    }

    osg::Vec4f TerrainWeights::computeVertexWeightDirect(
        const osg::Vec3f& vertexPos,
        const osg::Vec2f& chunkCenter,
        Storage* terrainStorage,
        ESM::RefId worldspace,
        float cellWorldSize)
    {
        if (!terrainStorage)
            return DEFAULT_ROCK_WEIGHT;

        // Convert vertex position from chunk-local to world coordinates
        osg::Vec2f worldPos2D;
        worldPos2D.x() = chunkCenter.x() * cellWorldSize + vertexPos.x();
        worldPos2D.y() = chunkCenter.y() * cellWorldSize + vertexPos.y();

        // Convert world position to cell coordinates
        osg::Vec2f cellPos;
        cellPos.x() = worldPos2D.x() / cellWorldSize;
        cellPos.y() = worldPos2D.y() / cellWorldSize;

        // Sample texture directly from land data using cell coordinates
        // This is the KEY FIX: same cell position always returns same texture,
        // regardless of which chunk is rendering the vertex
        std::string textureName = terrainStorage->getTextureAtPosition(cellPos, worldspace);

        if (textureName.empty())
            return DEFAULT_ROCK_WEIGHT; // No texture = rock (no deformation)

        // Classify the texture to get terrain type weight
        osg::Vec4f weight = classifyTexture(textureName);


        return weight;
    }

    osg::Vec4f TerrainWeights::classifyTexture(const std::string& texturePath)
    {
        // Use existing SnowDetection pattern matching
        if (SnowDetection::isSnowTexture(texturePath))
        {
            return osg::Vec4f(1.0f, 0.0f, 0.0f, 0.0f); // Pure snow
        }

        if (SnowDetection::isAshTexture(texturePath))
        {
            return osg::Vec4f(0.0f, 1.0f, 0.0f, 0.0f); // Pure ash
        }

        if (SnowDetection::isMudTexture(texturePath))
        {
            return osg::Vec4f(0.0f, 0.0f, 1.0f, 0.0f); // Pure mud
        }

        // Default: rock (no deformation)
        return osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f); // Pure rock
    }

    float TerrainWeights::sampleBlendmap(const osg::Image* blendmap, const osg::Vec2f& uv)
    {
        if (!blendmap || !blendmap->data())
            return 0.0f;

        // Clamp UV to [0, 1]
        float u = std::max(0.0f, std::min(1.0f, uv.x()));
        float v = std::max(0.0f, std::min(1.0f, uv.y()));

        // Convert to pixel coordinates
        int x = static_cast<int>(u * (blendmap->s() - 1));
        int y = static_cast<int>(v * (blendmap->t() - 1));

        // Clamp to image bounds
        x = std::max(0, std::min(x, blendmap->s() - 1));
        y = std::max(0, std::min(y, blendmap->t() - 1));

        // Get pixel data
        const unsigned char* pixel = blendmap->data(x, y);

        // Blendmaps typically store weight in alpha channel or as grayscale
        int bytesPerPixel = blendmap->getPixelSizeInBits() / 8;

        if (bytesPerPixel >= 4)
        {
            // RGBA format - use alpha channel
            return pixel[3] / 255.0f;
        }
        else if (bytesPerPixel >= 1)
        {
            // Grayscale - use red channel
            return pixel[0] / 255.0f;
        }

        return 0.0f;
    }

    osg::Vec4f TerrainWeights::interpolateWeights(const osg::Vec4f& w0, const osg::Vec4f& w1)
    {
        // Simple average
        osg::Vec4f result = (w0 + w1) * 0.5f;

        // Normalize to ensure sum = 1.0
        float sum = result.x() + result.y() + result.z() + result.w();
        if (sum > 0.001f)
            result /= sum;
        else
            result = DEFAULT_ROCK_WEIGHT;

        return result;
    }
}
