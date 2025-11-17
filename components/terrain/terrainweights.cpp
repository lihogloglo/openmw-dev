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

        // Calculate UV coordinates for vertex within chunk
        // vertexPos is in chunk-local coordinates (relative to chunk center)
        // Convert to UV [0,1] for blendmap sampling
        //
        // IMPORTANT: We clamp UV to slightly inside [0,1] to ensure consistent
        // sampling at chunk boundaries. Adjacent chunks will sample the same
        // blendmap values at their shared edges, preventing seams.
        float halfSize = (chunkSize * cellWorldSize) * 0.5f;
        float u = (vertexPos.x() + halfSize) / (chunkSize * cellWorldSize);
        float v = (vertexPos.y() + halfSize) / (chunkSize * cellWorldSize);

        // Clamp to [0, 1] to ensure we don't sample outside the blendmap
        // This is especially important at chunk boundaries
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
                               << " | Layers: " << layerList.size()
                               << " | Blendmaps: " << blendmaps.size();

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
                                << totalWeight.w() << ")";
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

        // BILINEAR INTERPOLATION
        // This creates smooth gradients even from binary blendmaps (0/1)
        // Instead of nearest-neighbor sampling, we interpolate between 4 pixels

        // Convert UV to continuous pixel coordinates
        float fx = u * (blendmap->s() - 1);
        float fy = v * (blendmap->t() - 1);

        // Get integer coordinates of 4 surrounding pixels
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        int x1 = std::min(x0 + 1, blendmap->s() - 1);
        int y1 = std::min(y0 + 1, blendmap->t() - 1);

        // Calculate interpolation weights
        float wx = fx - x0;  // Weight for x1 vs x0
        float wy = fy - y0;  // Weight for y1 vs y0

        // Sample 4 corner pixels
        auto getPixelValue = [&](int x, int y) -> float {
            const unsigned char* pixel = blendmap->data(x, y);
            int bytesPerPixel = blendmap->getPixelSizeInBits() / 8;

            if (bytesPerPixel >= 4)
                return pixel[3] / 255.0f;  // RGBA - use alpha
            else if (bytesPerPixel >= 1)
                return pixel[0] / 255.0f;  // Grayscale
            return 0.0f;
        };

        float v00 = getPixelValue(x0, y0);  // Top-left
        float v10 = getPixelValue(x1, y0);  // Top-right
        float v01 = getPixelValue(x0, y1);  // Bottom-left
        float v11 = getPixelValue(x1, y1);  // Bottom-right

        // Bilinear interpolation
        float top = v00 * (1.0f - wx) + v10 * wx;      // Interpolate top edge
        float bottom = v01 * (1.0f - wx) + v11 * wx;   // Interpolate bottom edge
        float blendValue = top * (1.0f - wy) + bottom * wy;  // Interpolate vertically

        // DEBUG: Log blendmap sampling (only occasionally to avoid spam)
        static int sampleCounter = 0;
        if (sampleCounter++ % 10000 == 0)
        {
            Log(Debug::Verbose) << "[TERRAIN WEIGHTS] Blendmap sample at UV(" << u << ", " << v
                               << ") pixel(" << fx << ", " << fy
                               << ") corners=(" << v00 << "," << v10 << "," << v01 << "," << v11 << ")"
                               << " → blended=" << blendValue
                               << " | Blendmap size: " << blendmap->s() << "x" << blendmap->t()
                               << " | Bytes/pixel: " << (blendmap->getPixelSizeInBits() / 8);
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
