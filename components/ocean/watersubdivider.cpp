#include "watersubdivider.hpp"

#include <osg/Array>
#include <osg/PrimitiveSet>

namespace Ocean
{
    osg::ref_ptr<osg::Geometry> WaterSubdivider::subdivide(const osg::Geometry* source, int levels)
    {
        if (!source || levels <= 0)
            return nullptr;

        // Get source arrays
        const osg::Vec3Array* srcVerts = dynamic_cast<const osg::Vec3Array*>(source->getVertexArray());
        const osg::Vec3Array* srcNormals = dynamic_cast<const osg::Vec3Array*>(source->getNormalArray());
        const osg::Vec2Array* srcUVs = dynamic_cast<const osg::Vec2Array*>(source->getTexCoordArray(0));

        if (!srcVerts)
            return nullptr;

        // Create output geometry
        osg::ref_ptr<osg::Geometry> result = new osg::Geometry;
        result->setDataVariance(osg::Object::STATIC);
        result->setUseDisplayList(false);
        result->setUseVertexBufferObjects(true);

        // Create destination arrays
        osg::ref_ptr<osg::Vec3Array> dstVerts = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec3Array> dstNormals = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec2Array> dstUVs = new osg::Vec2Array;

        // Process each primitive set
        for (unsigned int i = 0; i < source->getNumPrimitiveSets(); ++i)
        {
            const osg::DrawElements* primitives = dynamic_cast<const osg::DrawElements*>(source->getPrimitiveSet(i));
            if (!primitives || primitives->getMode() != GL_TRIANGLES)
                continue;

            subdivideTriangles(primitives, srcVerts, srcNormals, srcUVs,
                             dstVerts.get(), dstNormals.get(), dstUVs.get(), levels);
        }

        // Set arrays on result geometry
        result->setVertexArray(dstVerts.get());

        if (dstNormals->size() > 0)
        {
            result->setNormalArray(dstNormals.get(), osg::Array::BIND_PER_VERTEX);
        }

        if (dstUVs->size() > 0)
        {
            result->setTexCoordArray(0, dstUVs.get(), osg::Array::BIND_PER_VERTEX);
        }

        // Create primitive set for subdivided triangles
        if (dstVerts->size() > 0)
        {
            osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);
            indices->reserve(dstVerts->size());
            for (unsigned int i = 0; i < dstVerts->size(); ++i)
            {
                indices->push_back(i);
            }
            result->addPrimitiveSet(indices.get());
        }

        return result;
    }

    void WaterSubdivider::subdivideTriangles(
        const osg::DrawElements* primitives,
        const osg::Vec3Array* srcVerts,
        const osg::Vec3Array* srcNormals,
        const osg::Vec2Array* srcUVs,
        osg::Vec3Array* dstVerts,
        osg::Vec3Array* dstNormals,
        osg::Vec2Array* dstUVs,
        int levels)
    {
        // Process triangles in groups of 3 indices
        for (unsigned int i = 0; i + 2 < primitives->getNumIndices(); i += 3)
        {
            unsigned int i0 = primitives->index(i);
            unsigned int i1 = primitives->index(i + 1);
            unsigned int i2 = primitives->index(i + 2);

            // Get triangle vertices
            const osg::Vec3& v0 = (*srcVerts)[i0];
            const osg::Vec3& v1 = (*srcVerts)[i1];
            const osg::Vec3& v2 = (*srcVerts)[i2];

            // Get normals (or use default)
            osg::Vec3 n0(0, 0, 1), n1(0, 0, 1), n2(0, 0, 1);
            if (srcNormals && srcNormals->size() > i2)
            {
                n0 = (*srcNormals)[i0];
                n1 = (*srcNormals)[i1];
                n2 = (*srcNormals)[i2];
            }

            // Get UVs (or use default)
            osg::Vec2 uv0(0, 0), uv1(1, 0), uv2(0, 1);
            if (srcUVs && srcUVs->size() > i2)
            {
                uv0 = (*srcUVs)[i0];
                uv1 = (*srcUVs)[i1];
                uv2 = (*srcUVs)[i2];
            }

            // Subdivide this triangle
            subdivideTriangleRecursive(
                v0, v1, v2,
                n0, n1, n2,
                uv0, uv1, uv2,
                dstVerts, dstNormals, dstUVs,
                levels);
        }
    }

    void WaterSubdivider::subdivideTriangleRecursive(
        const osg::Vec3& v0, const osg::Vec3& v1, const osg::Vec3& v2,
        const osg::Vec3& n0, const osg::Vec3& n1, const osg::Vec3& n2,
        const osg::Vec2& uv0, const osg::Vec2& uv1, const osg::Vec2& uv2,
        osg::Vec3Array* dstV,
        osg::Vec3Array* dstN,
        osg::Vec2Array* dstUV,
        int level)
    {
        if (level <= 0)
        {
            // Base case: add triangle
            dstV->push_back(v0);
            dstV->push_back(v1);
            dstV->push_back(v2);

            dstN->push_back(n0);
            dstN->push_back(n1);
            dstN->push_back(n2);

            dstUV->push_back(uv0);
            dstUV->push_back(uv1);
            dstUV->push_back(uv2);
        }
        else
        {
            // Recursive case: split triangle into 4
            // Calculate edge midpoints
            osg::Vec3 v01 = (v0 + v1) * 0.5f;
            osg::Vec3 v12 = (v1 + v2) * 0.5f;
            osg::Vec3 v20 = (v2 + v0) * 0.5f;

            osg::Vec3 n01 = interpolateNormal(n0, n1);
            osg::Vec3 n12 = interpolateNormal(n1, n2);
            osg::Vec3 n20 = interpolateNormal(n2, n0);

            osg::Vec2 uv01 = (uv0 + uv1) * 0.5f;
            osg::Vec2 uv12 = (uv1 + uv2) * 0.5f;
            osg::Vec2 uv20 = (uv2 + uv0) * 0.5f;

            // Recursively subdivide the 4 sub-triangles
            subdivideTriangleRecursive(v0, v01, v20, n0, n01, n20, uv0, uv01, uv20, dstV, dstN, dstUV, level - 1);
            subdivideTriangleRecursive(v01, v1, v12, n01, n1, n12, uv01, uv1, uv12, dstV, dstN, dstUV, level - 1);
            subdivideTriangleRecursive(v20, v12, v2, n20, n12, n2, uv20, uv12, uv2, dstV, dstN, dstUV, level - 1);
            subdivideTriangleRecursive(v01, v12, v20, n01, n12, n20, uv01, uv12, uv20, dstV, dstN, dstUV, level - 1);
        }
    }

    osg::Vec3 WaterSubdivider::interpolateNormal(const osg::Vec3& n0, const osg::Vec3& n1)
    {
        osg::Vec3 result = (n0 + n1) * 0.5f;
        result.normalize();
        return result;
    }
}
