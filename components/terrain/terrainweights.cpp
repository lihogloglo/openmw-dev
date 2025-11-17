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

        // DEBUG: Log chunk information
        static int chunkCounter = 0;
        if (chunkCounter++ % 100 == 0)
        {
            Log(Debug::Info) << "[TERRAIN WEIGHTS] Computing weights for chunk at ("
                            << chunkCenter.x() << ", " << chunkCenter.y()
                            << ") size=" << chunkSize
                            << " | Layers=" << layerList.size()
                            << " | Blendmaps=" << blendmaps.size()
                            << " | Vertices=" << vertices->size()
                            << " | cellWorldSize=" << cellWorldSize;
        }

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
            osg::Vec4f chunkWeight = computeVertexWeight(
                centerVertex, chunkCenter, chunkSize, layerList, blendmaps, cellWorldSize);

            // Apply same weight to all vertices
            for (size_t i = 0; i < vertices->size(); ++i)
                weights->push_back(chunkWeight);

            return weights;
        }

        // LOD_FULL: Compute per-vertex weights
        for (size_t i = 0; i < vertices->size(); ++i)
        {
            const osg::Vec3f& vertexPos = (*vertices)[i];
            osg::Vec4f weight = computeVertexWeight(
                vertexPos, chunkCenter, chunkSize, layerList, blendmaps, cellWorldSize);
            weights->push_back(weight);
        }

        return weights;
    }

    osg::Vec4f TerrainWeights::computeVertexWeight(
        const osg::Vec3f& vertexPos,
        const osg::Vec2f& chunkCenter,
        float chunkSize,
        const std::vector<LayerInfo>& layerList,
        const std::vector<osg::ref_ptr<osg::Image>>& blendmaps,
        float cellWorldSize)
    {
        // If no layers, default to rock
        if (layerList.empty())
            return DEFAULT_ROCK_WEIGHT;

        // ============================================================================
        // BOUNDARY VERTEX SHARING - Grid-Snapping Approach
        // ============================================================================
        // To eliminate seams at chunk boundaries, we snap vertex positions to the
        // land texture grid. This ensures that boundary vertices in adjacent chunks
        // round to the same grid position and sample the same blendmap texel.
        //
        // How it works:
        // 1. Convert vertex position to world coordinates
        // 2. Calculate position in land texture grid (texels per cell)
        // 3. Round to nearest land texel - THIS IS THE KEY for boundary sharing!
        // 4. Convert back to UV within the chunk's blendmap coverage
        // 5. Sample using nearest-neighbor (no bilinear, kept simple)
        //
        // Why this eliminates seams:
        // - Boundary vertices in adjacent chunks have the same world position
        // - They round to the same land texel
        // - They sample the same data from their respective blendmaps
        // ============================================================================

        // Convert chunk-local vertex position to world-space
        // chunkCenter is in cell coordinates (e.g., 0.5, 1.5)
        // cellWorldSize is world units per cell (e.g., 8192 for Morrowind)
        float worldX = chunkCenter.x() * cellWorldSize + vertexPos.x();
        float worldY = chunkCenter.y() * cellWorldSize + vertexPos.y();

        // Convert world position to cell-space coordinates
        float cellX = worldX / cellWorldSize;
        float cellY = worldY / cellWorldSize;

        // Convert to land texture grid coordinates (Morrowind uses 16x16 texels per cell)
        const int LAND_TEXTURE_SIZE = 16;  // ESM::Land::LAND_TEXTURE_SIZE
        float landTexelX = cellX * LAND_TEXTURE_SIZE;
        float landTexelY = cellY * LAND_TEXTURE_SIZE;

        // CRITICAL: Round to nearest land texel
        // This makes boundary vertices in adjacent chunks sample the SAME texel!
        int snappedTexelX = static_cast<int>(std::round(landTexelX));
        int snappedTexelY = static_cast<int>(std::round(landTexelY));

        // Convert snapped texel position back to UV within this chunk's blendmap
        // The chunk's blendmap covers a specific range of land texels
        osg::Vec2f origin = chunkCenter - osg::Vec2f(chunkSize, chunkSize) * 0.5f;
        int startTexelX = static_cast<int>(std::floor(origin.x() * LAND_TEXTURE_SIZE));
        int startTexelY = static_cast<int>(std::floor(origin.y() * LAND_TEXTURE_SIZE));

        // Calculate how many texels the blendmap covers
        // CRITICAL: Blendmaps include +1 for boundary overlap (see gridsampling.hpp:getBlendmapSize)
        // This ensures adjacent chunks share boundary texels!
        float blendmapTexelCount = chunkSize * LAND_TEXTURE_SIZE + 1.0f;

        // UV coordinates within the chunk's blendmap [0, 1]
        // For a 0.125 chunk: blendmapTexelCount = 3, so texels map to: 0→0.0, 1→0.5, 2→1.0
        float u = static_cast<float>(snappedTexelX - startTexelX) / (blendmapTexelCount - 1.0f);
        float v = static_cast<float>(snappedTexelY - startTexelY) / (blendmapTexelCount - 1.0f);

        // Clamp to [0, 1] to handle edge cases
        u = std::max(0.0f, std::min(1.0f, u));
        v = std::max(0.0f, std::min(1.0f, v));

        osg::Vec2f uv(u, v);

        // Accumulate weights from all layers
        osg::Vec4f totalWeight(0.0f, 0.0f, 0.0f, 0.0f);

        // First layer is always fully visible (base layer)
        if (!layerList.empty())
        {
            osg::Vec4f layerType = classifyTexture(layerList[0].mDiffuseMap.value());
            totalWeight += layerType; // Full weight for base layer initially
        }

        // Additional layers blend based on blendmaps
        for (size_t i = 1; i < layerList.size() && i - 1 < blendmaps.size(); ++i)
        {
            const LayerInfo& layer = layerList[i];
            const osg::Image* blendmap = blendmaps[i - 1].get();

            // Sample blendmap to get this layer's influence
            float blend = sampleBlendmap(blendmap, uv);

            if (blend > 0.001f) // Skip negligible blends
            {
                osg::Vec4f layerType = classifyTexture(layer.mDiffuseMap.value());

                // Blend this layer in, reducing base layer proportionally
                totalWeight = totalWeight * (1.0f - blend) + layerType * blend;
            }
        }

        // Normalize to ensure weights sum to 1.0
        float sum = totalWeight.x() + totalWeight.y() + totalWeight.z() + totalWeight.w();
        if (sum > 0.001f)
            totalWeight /= sum;
        else
            totalWeight = DEFAULT_ROCK_WEIGHT;

        // Debug logging (only log occasionally to avoid spam)
        static int logCounter = 0;
        if (logCounter++ % 1000 == 0)
        {
            Log(Debug::Verbose) << "[TERRAIN WEIGHTS] Sample vertex weight: ("
                               << totalWeight.x() << " snow, "
                               << totalWeight.y() << " ash, "
                               << totalWeight.z() << " mud, "
                               << totalWeight.w() << " rock)"
                               << " | World UV: (" << uv.x() << ", " << uv.y() << ")"
                               << " | World pos: (" << worldX << ", " << worldY << ")"
                               << " | Chunk: (" << chunkCenter.x() << ", " << chunkCenter.y() << ")"
                               << " | Layers: " << layerList.size();

            // Log if we have a truly blended weight (not binary)
            bool isBinary = (totalWeight.x() < 0.01f || totalWeight.x() > 0.99f) &&
                           (totalWeight.y() < 0.01f || totalWeight.y() > 0.99f) &&
                           (totalWeight.z() < 0.01f || totalWeight.z() > 0.99f) &&
                           (totalWeight.w() < 0.01f || totalWeight.w() > 0.99f);

            if (!isBinary)
            {
                Log(Debug::Info) << "[TERRAIN WEIGHTS] FOUND BLENDED WEIGHT! ("
                                << totalWeight.x() << ", "
                                << totalWeight.y() << ", "
                                << totalWeight.z() << ", "
                                << totalWeight.w() << ")"
                                << " at world (" << worldX << ", " << worldY << ")";
            }
        }

        return totalWeight;
    }

    osg::Vec4f TerrainWeights::classifyTexture(const std::string& texturePath)
    {
        // Use existing SnowDetection pattern matching
        if (SnowDetection::isSnowTexture(texturePath))
        {
            Log(Debug::Verbose) << "[TERRAIN WEIGHTS] Classified texture as SNOW: " << texturePath;
            return osg::Vec4f(1.0f, 0.0f, 0.0f, 0.0f); // Pure snow
        }

        if (SnowDetection::isAshTexture(texturePath))
        {
            Log(Debug::Verbose) << "[TERRAIN WEIGHTS] Classified texture as ASH: " << texturePath;
            return osg::Vec4f(0.0f, 1.0f, 0.0f, 0.0f); // Pure ash
        }

        if (SnowDetection::isMudTexture(texturePath))
        {
            Log(Debug::Verbose) << "[TERRAIN WEIGHTS] Classified texture as MUD: " << texturePath;
            return osg::Vec4f(0.0f, 0.0f, 1.0f, 0.0f); // Pure mud
        }

        // Default: rock (no deformation)
        Log(Debug::Verbose) << "[TERRAIN WEIGHTS] Classified texture as ROCK (no deformation): " << texturePath;
        return osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f); // Pure rock
    }

    float TerrainWeights::sampleBlendmap(const osg::Image* blendmap, const osg::Vec2f& uv)
    {
        if (!blendmap || !blendmap->data())
            return 0.0f;

        // Clamp UV to [0, 1]
        float u = std::max(0.0f, std::min(1.0f, uv.x()));
        float v = std::max(0.0f, std::min(1.0f, uv.y()));

        // NEAREST NEIGHBOR SAMPLING
        // Simple and fast - no bilinear interpolation needed since we're using grid-snapping
        // The grid-snapping in computeVertexWeight() already ensures boundary vertices
        // sample the same texel, so we don't need smooth interpolation here.

        // Convert UV to pixel coordinates and round to nearest
        int x = static_cast<int>(std::round(u * (blendmap->s() - 1)));
        int y = static_cast<int>(std::round(v * (blendmap->t() - 1)));

        // Clamp to valid range
        x = std::max(0, std::min(x, blendmap->s() - 1));
        y = std::max(0, std::min(y, blendmap->t() - 1));

        // Sample the pixel
        const unsigned char* pixel = blendmap->data(x, y);
        int bytesPerPixel = blendmap->getPixelSizeInBits() / 8;

        float blendValue;
        if (bytesPerPixel >= 4)
            blendValue = pixel[3] / 255.0f;  // RGBA - use alpha
        else if (bytesPerPixel >= 1)
            blendValue = pixel[0] / 255.0f;  // Grayscale
        else
            blendValue = 0.0f;

        // DEBUG: Log blendmap sampling (only occasionally to avoid spam)
        static int sampleCounter = 0;
        if (sampleCounter++ % 10000 == 0)
        {
            Log(Debug::Verbose) << "[TERRAIN WEIGHTS] Blendmap sample at UV(" << u << ", " << v
                               << ") → pixel(" << x << ", " << y << ")"
                               << " → value=" << blendValue
                               << " | Blendmap size: " << blendmap->s() << "x" << blendmap->t()
                               << " | Bytes/pixel: " << bytesPerPixel;
        }

        return blendValue;
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
