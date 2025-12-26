#include "terraindrawable.hpp"

#include <osg/ClusterCullingCallback>
#include <osg/Uniform>
#include <osgUtil/CullVisitor>

#include <components/sceneutil/lightmanager.hpp>
#include <components/settings/values.hpp>

#include "compositemaprenderer.hpp"
#include "displacementmaprenderer.hpp"

namespace Terrain
{

    TerrainDrawable::TerrainDrawable() {}

    TerrainDrawable::~TerrainDrawable() {}

    TerrainDrawable::TerrainDrawable(const TerrainDrawable& copy, const osg::CopyOp& copyop)
        : osg::Geometry(copy, copyop)
        , mPasses(copy.mPasses)
        , mLightListCallback(copy.mLightListCallback)
    {
    }

    void TerrainDrawable::accept(osg::NodeVisitor& nv)
    {
        if (nv.getVisitorType() != osg::NodeVisitor::CULL_VISITOR)
        {
            osg::Geometry::accept(nv);
        }
        else if (nv.validNodeMask(*this))
        {
            nv.pushOntoNodePath(this);
            cull(static_cast<osgUtil::CullVisitor*>(&nv));
            nv.popFromNodePath();
        }
    }

    inline float distance(const osg::Vec3& coord, const osg::Matrix& matrix)
    {
        return static_cast<float>(-((double)coord[0] * matrix(0, 2) + (double)coord[1] * matrix(1, 2)
            + (double)coord[2] * matrix(2, 2) + matrix(3, 2)));
    }

    // canot use ClusterCullingCallback::cull: viewpoint != eyepoint
    //  !osgfixpotential!
    bool clusterCull(osg::ClusterCullingCallback* cb, const osg::Vec3f& eyePoint, bool shadowcam)
    {
        const float deviation = cb->getDeviation();
        const osg::Vec3& controlPoint = cb->getControlPoint();
        osg::Vec3 normal = cb->getNormal();
        if (shadowcam)
            normal = normal * -1; // inverting for shadowcam frontfaceculing
        if (deviation <= -1.0f)
            return false;
        const osg::Vec3 eyeControlPoint = eyePoint - controlPoint;
        const float radius = eyeControlPoint.length();
        if (radius < cb->getRadius())
            return false;
        return (eyeControlPoint * normal) / radius < deviation;
    }

    void TerrainDrawable::cull(osgUtil::CullVisitor* cv)
    {
        const osg::BoundingBox& bb = getBoundingBox();

        if (_cullingActive && cv->isCulled(getBoundingBox()))
            return;

        bool shadowcam = cv->getCurrentCamera()->getName() == "ShadowCamera";

        if (cv->getCullingMode() & osg::CullStack::CLUSTER_CULLING
            && clusterCull(mClusterCullingCallback, cv->getEyePoint(), shadowcam))
            return;

        osg::RefMatrix& matrix = *cv->getModelViewMatrix();

        if (cv->getComputeNearFarMode() != osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR && bb.valid())
        {
            if (!cv->updateCalculatedNearFar(matrix, *this, false))
                return;
        }

        float depth = bb.valid() ? distance(bb.center(), matrix) : 0.0f;
        if (osg::isNaN(depth))
            return;

        if (shadowcam)
        {
            cv->addDrawableAndDepth(this, &matrix, depth);
            return;
        }

        if (mCompositeMap && mCompositeMapRenderer)
        {
            mCompositeMapRenderer->setImmediate(mCompositeMap);
            mCompositeMapRenderer = nullptr;
        }

        if (mDisplacementMap && mDisplacementMapRenderer)
        {
            mDisplacementMapRenderer->setImmediate(mDisplacementMap);
            mDisplacementMapRenderer = nullptr;
        }

        bool pushedLight = mLightListCallback && mLightListCallback->pushLightState(this, cv);

        osg::StateSet* stateset = getStateSet();

        // Dynamically update tessellation and displacement uniforms
        // This ensures settings changes take effect immediately without requiring chunk reload
        // Note: cameraPos uniform is no longer needed - the shader now computes camera position
        // in local chunk space from gl_ModelViewMatrixInverse for correct distance calculations
        if (stateset)
        {
            cv->pushStateSet(stateset);
        }

        // Update tessellation and displacement uniforms in each pass
        // These uniforms need to reflect current settings values for live updates
        for (PassVector::const_iterator it = mPasses.begin(); it != mPasses.end(); ++it)
        {
            osg::StateSet* passStateset = it->get();
            if (!passStateset)
                continue;

            // Update tessellation distance/level uniforms
            if (osg::Uniform* u = passStateset->getUniform("tessMinDistance"))
                u->set(Settings::terrain().mTessellationMinDistance.get());
            if (osg::Uniform* u = passStateset->getUniform("tessMaxDistance"))
                u->set(Settings::terrain().mTessellationMaxDistance.get());
            if (osg::Uniform* u = passStateset->getUniform("tessMinLevel"))
                u->set(Settings::terrain().mTessellationMinLevel.get());
            if (osg::Uniform* u = passStateset->getUniform("tessMaxLevel"))
                u->set(Settings::terrain().mTessellationMaxLevel.get());

            // Update displacement uniforms
            // All passes use the same displacement map, so they all displace identically
            if (osg::Uniform* u = passStateset->getUniform("heightmapDisplacementEnabled"))
                u->set(Settings::terrain().mHeightmapDisplacement.get());
            if (osg::Uniform* u = passStateset->getUniform("heightmapDisplacementStrength"))
                u->set(Settings::terrain().mHeightmapDisplacementStrength.get());
        }

        for (PassVector::const_iterator it = mPasses.begin(); it != mPasses.end(); ++it)
        {
            cv->pushStateSet(*it);
            cv->addDrawableAndDepth(this, &matrix, depth);
            cv->popStateSet();
        }

        if (stateset)
            cv->popStateSet();
        if (pushedLight)
            cv->popStateSet();
    }

    void TerrainDrawable::createClusterCullingCallback()
    {
        mClusterCullingCallback = new osg::ClusterCullingCallback(this);
    }

    void TerrainDrawable::setPasses(const TerrainDrawable::PassVector& passes)
    {
        mPasses = passes;
    }

    void TerrainDrawable::setLightListCallback(SceneUtil::LightListCallback* lightListCallback)
    {
        mLightListCallback = lightListCallback;
    }

    void TerrainDrawable::setupWaterBoundingBox(float waterheight, float margin)
    {
        osg::Vec3Array* vertices = static_cast<osg::Vec3Array*>(getVertexArray());
        for (unsigned int i = 0; i < vertices->size(); ++i)
        {
            const osg::Vec3f& vertex = (*vertices)[i];
            if (vertex.z() <= waterheight)
                mWaterBoundingBox.expandBy(vertex);
        }
        if (mWaterBoundingBox.valid())
        {
            const osg::BoundingBox& bb = getBoundingBox();
            mWaterBoundingBox.xMin() = std::max(bb.xMin(), mWaterBoundingBox.xMin() - margin);
            mWaterBoundingBox.yMin() = std::max(bb.yMin(), mWaterBoundingBox.yMin() - margin);
            mWaterBoundingBox.xMax() = std::min(bb.xMax(), mWaterBoundingBox.xMax() + margin);
            mWaterBoundingBox.xMax() = std::min(bb.xMax(), mWaterBoundingBox.xMax() + margin);
        }
    }

    void TerrainDrawable::compileGLObjects(osg::RenderInfo& renderInfo) const
    {
        for (PassVector::const_iterator it = mPasses.begin(); it != mPasses.end(); ++it)
        {
            osg::StateSet* stateset = *it;
            stateset->compileGLObjects(*renderInfo.getState());
        }

        osg::Geometry::compileGLObjects(renderInfo);
    }

}
