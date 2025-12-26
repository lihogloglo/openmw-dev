#include "buffercache.hpp"

#include <cassert>

#include <osg/PrimitiveSet>

#include "defs.hpp"

namespace
{

    // Forward declaration
    template <typename IndexArrayType>
    osg::ref_ptr<IndexArrayType> createIndexBufferImpl(osg::ref_ptr<IndexArrayType> indices, unsigned int flags, unsigned int verts);

    template <typename IndexArrayType>
    osg::ref_ptr<IndexArrayType> createIndexBuffer(unsigned int flags, unsigned int verts)
    {
        osg::ref_ptr<IndexArrayType> indices(new IndexArrayType(osg::PrimitiveSet::TRIANGLES));
        return createIndexBufferImpl<IndexArrayType>(indices, flags, verts);
    }

    template <typename IndexArrayType>
    osg::ref_ptr<IndexArrayType> createPatchIndexBuffer(unsigned int flags, unsigned int verts)
    {
        using IndexType = typename IndexArrayType::value_type;
        // For quad tessellation, use GL_PATCHES with 4 vertices per patch
        // Create quad patches directly from the terrain grid
        osg::ref_ptr<IndexArrayType> indices(new IndexArrayType(GL_PATCHES, 0, 4));

        // LOD level n means every 2^n-th vertex is kept, but we currently handle LOD elsewhere.
        size_t lodLevel = 0;

        size_t lodDeltas[4];
        for (int i = 0; i < 4; ++i)
            lodDeltas[i] = (flags >> (4 * i)) & (0xf);

        bool anyDeltas = (lodDeltas[Terrain::North] || lodDeltas[Terrain::South] || lodDeltas[Terrain::West]
            || lodDeltas[Terrain::East]);

        size_t increment = static_cast<size_t>(1) << lodLevel;
        assert(increment < verts);
        indices->reserve((verts - 1) * (verts - 1) * 4 / increment);

        size_t rowStart = 0, colStart = 0, rowEnd = verts - 1, colEnd = verts - 1;
        // If any edge needs stitching we'll skip edges (same as triangle version)
        if (anyDeltas)
        {
            colStart += increment;
            colEnd -= increment;
            rowEnd -= increment;
            rowStart += increment;
        }

        // Generate quad patches
        // Each quad is defined by 4 vertices in order: bottom-left, bottom-right, top-right, top-left
        for (size_t row = rowStart; row < rowEnd; row += increment)
        {
            for (size_t col = colStart; col < colEnd; col += increment)
            {
                // Quad vertices ordered for CCW winding when viewed from above:
                // 0: bottom-left  (col, row)
                // 1: bottom-right (col+1, row)
                // 2: top-right    (col+1, row+1)
                // 3: top-left     (col, row+1)
                indices->push_back(static_cast<IndexType>(verts * col + row));                           // 0: bottom-left
                indices->push_back(static_cast<IndexType>(verts * (col + increment) + row));             // 1: bottom-right
                indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + increment)); // 2: top-right
                indices->push_back(static_cast<IndexType>(verts * col + row + increment));               // 3: top-left
            }
        }

        // Handle LOD edge stitching for quad patches
        // For edges that need different LOD, we still need to generate quads
        // but with adjusted vertex positions to match the neighboring chunk
        if (anyDeltas)
        {
            // South edge (row = 0)
            size_t row = 0;
            size_t outerStep = static_cast<size_t>(1) << (lodDeltas[Terrain::South] + lodLevel);
            for (size_t col = 0; col < verts - 1; col += outerStep)
            {
                // Generate transition quads along the south edge
                // These need to connect outer edge vertices to inner grid
                for (size_t i = 0; i < outerStep && col + i < verts - 1; i += increment)
                {
                    if (col + i == 0 || col + i + increment > verts - 1)
                        continue;
                    // Create a quad that bridges the LOD boundary
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row));                 // bottom-left (on edge)
                    indices->push_back(static_cast<IndexType>(verts * (col + i + increment) + row));     // bottom-right (on edge)
                    indices->push_back(static_cast<IndexType>(verts * (col + i + increment) + row + increment)); // top-right (inner)
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row + increment));     // top-left (inner)
                }
            }

            // North edge (row = verts - 1)
            row = verts - 1 - increment;
            outerStep = static_cast<size_t>(1) << (lodDeltas[Terrain::North] + lodLevel);
            for (size_t col = 0; col < verts - 1; col += outerStep)
            {
                for (size_t i = 0; i < outerStep && col + i < verts - 1; i += increment)
                {
                    if (col + i == 0 || col + i + increment > verts - 1)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + i + increment) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + i + increment) + row + increment));
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row + increment));
                }
            }

            // West edge (col = 0)
            size_t col = 0;
            outerStep = static_cast<size_t>(1) << (lodDeltas[Terrain::West] + lodLevel);
            for (row = 0; row < verts - 1; row += outerStep)
            {
                for (size_t i = 0; i < outerStep && row + i < verts - 1; i += increment)
                {
                    if (row + i == 0 || row + i + increment > verts - 1)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * col + row + i));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + i));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + i + increment));
                    indices->push_back(static_cast<IndexType>(verts * col + row + i + increment));
                }
            }

            // East edge (col = verts - 1)
            col = verts - 1 - increment;
            outerStep = static_cast<size_t>(1) << (lodDeltas[Terrain::East] + lodLevel);
            for (row = 0; row < verts - 1; row += outerStep)
            {
                for (size_t i = 0; i < outerStep && row + i < verts - 1; i += increment)
                {
                    if (row + i == 0 || row + i + increment > verts - 1)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * col + row + i));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + i));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + i + increment));
                    indices->push_back(static_cast<IndexType>(verts * col + row + i + increment));
                }
            }
        }

        return indices;
    }

    template <typename IndexArrayType>
    osg::ref_ptr<IndexArrayType> createIndexBufferImpl(osg::ref_ptr<IndexArrayType> indices, unsigned int flags, unsigned int verts)
    {
        using IndexType = typename IndexArrayType::value_type;
        // LOD level n means every 2^n-th vertex is kept, but we currently handle LOD elsewhere.
        size_t lodLevel = 0; //(flags >> (4*4));

        size_t lodDeltas[4];
        for (int i = 0; i < 4; ++i)
            lodDeltas[i] = (flags >> (4 * i)) & (0xf);

        bool anyDeltas = (lodDeltas[Terrain::North] || lodDeltas[Terrain::South] || lodDeltas[Terrain::West]
            || lodDeltas[Terrain::East]);

        size_t increment = static_cast<size_t>(1) << lodLevel;
        assert(increment < verts);
        indices->reserve((verts - 1) * (verts - 1) * 2 * 3 / increment);

        size_t rowStart = 0, colStart = 0, rowEnd = verts - 1, colEnd = verts - 1;
        // If any edge needs stitching we'll skip all edges at this point,
        // mainly because stitching one edge would have an effect on corners and on the adjacent edges
        if (anyDeltas)
        {
            colStart += increment;
            colEnd -= increment;
            rowEnd -= increment;
            rowStart += increment;
        }
        for (size_t row = rowStart; row < rowEnd; row += increment)
        {
            for (size_t col = colStart; col < colEnd; col += increment)
            {
                // diamond pattern
                if ((row + col % 2) % 2 == 1)
                {
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + increment));
                    indices->push_back(static_cast<IndexType>(verts * col + row + increment));

                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col) + row + increment));
                }
                else
                {
                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + increment));
                    indices->push_back(static_cast<IndexType>(verts * col + row + increment));

                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + increment));
                }
            }
        }

        size_t innerStep = increment;
        if (anyDeltas)
        {
            // Now configure LOD transitions at the edges - this is pretty tedious,
            // and some very long and boring code, but it works great

            // South
            size_t row = 0;
            size_t outerStep = static_cast<size_t>(1) << (lodDeltas[Terrain::South] + lodLevel);
            for (size_t col = 0; col < verts - 1; col += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * col + row));
                indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row));
                // Make sure not to touch the right edge
                if (col + outerStep == verts - 1)
                    indices->push_back(static_cast<IndexType>(verts * (col + outerStep - innerStep) + row + innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row + innerStep));

                for (size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the left or right edges
                    if (col + i == 0 || col + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * (col) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + i + innerStep) + row + innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row + innerStep));
                }
            }

            // North
            row = verts - 1;
            outerStep = size_t(1) << (lodDeltas[Terrain::North] + lodLevel);
            for (size_t col = 0; col < verts - 1; col += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row));
                indices->push_back(static_cast<IndexType>(verts * col + row));
                // Make sure not to touch the left edge
                if (col == 0)
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row - innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * col + row - innerStep));

                for (size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the left or right edges
                    if (col + i == 0 || col + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row - innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col + i + innerStep) + row - innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row));
                }
            }

            // West
            size_t col = 0;
            outerStep = size_t(1) << (lodDeltas[Terrain::West] + lodLevel);
            for (row = 0; row < verts - 1; row += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * col + row + outerStep));
                indices->push_back(static_cast<IndexType>(verts * col + row));
                // Make sure not to touch the top edge
                if (row + outerStep == verts - 1)
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + outerStep - innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + outerStep));

                for (size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the top or bottom edges
                    if (row + i == 0 || row + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + i));
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + i + innerStep));
                }
            }

            // East
            col = verts - 1;
            outerStep = size_t(1) << (lodDeltas[Terrain::East] + lodLevel);
            for (row = 0; row < verts - 1; row += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * col + row));
                indices->push_back(static_cast<IndexType>(verts * col + row + outerStep));
                // Make sure not to touch the bottom edge
                if (row == 0)
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row + innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row));

                for (size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the top or bottom edges
                    if (row + i == 0 || row + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * col + row + outerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row + i + innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row + i));
                }
            }
        }

        return indices;
    }

}

namespace Terrain
{

    osg::ref_ptr<osg::Vec2Array> BufferCache::getUVBuffer(unsigned int numVerts)
    {
        std::lock_guard<std::mutex> lock(mUvBufferMutex);
        if (mUvBufferMap.find(numVerts) != mUvBufferMap.end())
        {
            return mUvBufferMap[numVerts];
        }

        int vertexCount = numVerts * numVerts;

        osg::ref_ptr<osg::Vec2Array> uvs(new osg::Vec2Array(osg::Array::BIND_PER_VERTEX));
        uvs->reserve(vertexCount);

        for (unsigned int col = 0; col < numVerts; ++col)
        {
            for (unsigned int row = 0; row < numVerts; ++row)
            {
                uvs->push_back(osg::Vec2f(
                    col / static_cast<float>(numVerts - 1), ((numVerts - 1) - row) / static_cast<float>(numVerts - 1)));
            }
        }

        // Assign a VBO here to enable state sharing between different Geometries.
        uvs->setVertexBufferObject(new osg::VertexBufferObject);

        mUvBufferMap[numVerts] = uvs;
        return uvs;
    }

    osg::ref_ptr<osg::DrawElements> BufferCache::getIndexBuffer(unsigned int numVerts, unsigned int flags)
    {
        std::pair<int, int> id = std::make_pair(numVerts, flags);
        std::lock_guard<std::mutex> lock(mIndexBufferMutex);

        if (mIndexBufferMap.find(id) != mIndexBufferMap.end())
        {
            return mIndexBufferMap[id];
        }

        osg::ref_ptr<osg::DrawElements> buffer;

        if (numVerts * numVerts <= (0xffffu))
            buffer = createIndexBuffer<osg::DrawElementsUShort>(flags, numVerts);
        else
            buffer = createIndexBuffer<osg::DrawElementsUInt>(flags, numVerts);

        // Assign a EBO here to enable state sharing between different Geometries.
        buffer->setElementBufferObject(new osg::ElementBufferObject);

        mIndexBufferMap[id] = buffer;
        return buffer;
    }

    osg::ref_ptr<osg::DrawElements> BufferCache::getPatchIndexBuffer(unsigned int numVerts, unsigned int flags)
    {
        std::pair<int, int> id = std::make_pair(numVerts, flags);
        std::lock_guard<std::mutex> lock(mPatchIndexBufferMutex);

        if (mPatchIndexBufferMap.find(id) != mPatchIndexBufferMap.end())
        {
            return mPatchIndexBufferMap[id];
        }

        osg::ref_ptr<osg::DrawElements> buffer;

        if (numVerts * numVerts <= (0xffffu))
            buffer = createPatchIndexBuffer<osg::DrawElementsUShort>(flags, numVerts);
        else
            buffer = createPatchIndexBuffer<osg::DrawElementsUInt>(flags, numVerts);

        // Assign a EBO here to enable state sharing between different Geometries.
        buffer->setElementBufferObject(new osg::ElementBufferObject);

        mPatchIndexBufferMap[id] = buffer;
        return buffer;
    }

    void BufferCache::clearCache()
    {
        {
            std::lock_guard<std::mutex> lock(mIndexBufferMutex);
            mIndexBufferMap.clear();
        }
        {
            std::lock_guard<std::mutex> lock(mPatchIndexBufferMutex);
            mPatchIndexBufferMap.clear();
        }
        {
            std::lock_guard<std::mutex> lock(mUvBufferMutex);
            mUvBufferMap.clear();
        }
    }

    void BufferCache::releaseGLObjects(osg::State* state)
    {
        {
            std::lock_guard<std::mutex> lock(mIndexBufferMutex);
            for (const auto& [_, indexbuffer] : mIndexBufferMap)
                indexbuffer->releaseGLObjects(state);
        }
        {
            std::lock_guard<std::mutex> lock(mPatchIndexBufferMutex);
            for (const auto& [_, indexbuffer] : mPatchIndexBufferMap)
                indexbuffer->releaseGLObjects(state);
        }
        {
            std::lock_guard<std::mutex> lock(mUvBufferMutex);
            for (const auto& [_, uvbuffer] : mUvBufferMap)
                uvbuffer->releaseGLObjects(state);
        }
    }

}
