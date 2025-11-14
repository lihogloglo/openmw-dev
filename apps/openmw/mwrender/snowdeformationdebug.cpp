#include "snowdeformationdebug.hpp"
#include "snowdeformation.hpp"
#include "vismask.hpp"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/PolygonMode>
#include <osg/LineWidth>
#include <osg/Material>
#include <osg/BlendFunc>
#include <osg/Depth>

#include <components/debug/debuglog.hpp>

namespace MWRender
{

SnowDeformationDebugger::SnowDeformationDebugger(osg::ref_ptr<osg::Group> rootNode, SnowDeformationManager* manager)
    : mRootNode(rootNode)
    , mManager(manager)
    , mEnabled(false)
    , mShowTextureOverlay(false)
    , mShowFootprintMarkers(false)
    , mShowMeshWireframe(false)
    , mShowDeformationBounds(false)
{
    mDebugGroup = new osg::Group;
    mDebugGroup->setName("SnowDeformationDebugGroup");
    mDebugGroup->setNodeMask(0); // Hidden by default
    mRootNode->addChild(mDebugGroup);

    Log(Debug::Info) << "SnowDeformationDebugger: Initialized";
}

SnowDeformationDebugger::~SnowDeformationDebugger()
{
    if (mDebugGroup && mRootNode)
    {
        mRootNode->removeChild(mDebugGroup);
    }
}

void SnowDeformationDebugger::setEnabled(bool enabled)
{
    mEnabled = enabled;
    mDebugGroup->setNodeMask(enabled ? ~0 : 0);
    Log(Debug::Info) << "SnowDeformationDebugger: " << (enabled ? "ENABLED" : "DISABLED");
}

void SnowDeformationDebugger::setShowTextureOverlay(bool show)
{
    mShowTextureOverlay = show;
    if (show && !mTextureOverlayGeode)
    {
        createTextureOverlay();
    }
    if (mTextureOverlayGeode)
    {
        mTextureOverlayGeode->setNodeMask(show ? ~0 : 0);
    }
    Log(Debug::Info) << "SnowDeformationDebugger: Texture overlay " << (show ? "ON" : "OFF");
}

void SnowDeformationDebugger::setShowFootprintMarkers(bool show)
{
    mShowFootprintMarkers = show;
    if (show && !mFootprintMarkersGroup)
    {
        createFootprintMarkers();
    }
    if (mFootprintMarkersGroup)
    {
        mFootprintMarkersGroup->setNodeMask(show ? ~0 : 0);
    }
    Log(Debug::Info) << "SnowDeformationDebugger: Footprint markers " << (show ? "ON" : "OFF");
}

void SnowDeformationDebugger::setShowMeshWireframe(bool show)
{
    mShowMeshWireframe = show;
    Log(Debug::Info) << "SnowDeformationDebugger: Mesh wireframe " << (show ? "ON" : "OFF");
    // This would need to modify the deformation mesh's state set
}

void SnowDeformationDebugger::setShowDeformationBounds(bool show)
{
    mShowDeformationBounds = show;
    if (show && !mBoundsGeode)
    {
        createBoundsVisualization();
    }
    if (mBoundsGeode)
    {
        mBoundsGeode->setNodeMask(show ? ~0 : 0);
    }
    Log(Debug::Info) << "SnowDeformationDebugger: Deformation bounds " << (show ? "ON" : "OFF");
}

void SnowDeformationDebugger::setDebugShaderOutput(bool enable)
{
    Log(Debug::Info) << "SnowDeformationDebugger: Debug shader output " << (enable ? "ON" : "OFF");
    // This would need to add a define to the terrain shader
}

void SnowDeformationDebugger::update(const osg::Vec3f& cameraPos)
{
    if (!mEnabled)
        return;

    if (mShowFootprintMarkers)
    {
        updateFootprintMarkers(cameraPos);
    }

    if (mShowDeformationBounds)
    {
        updateBoundsVisualization();
    }
}

void SnowDeformationDebugger::createTextureOverlay()
{
    mTextureOverlayGeode = new osg::Geode;
    mTextureOverlayGeode->setName("DeformationTextureOverlay");

    // Create a quad in screen space (HUD)
    osg::ref_ptr<osg::Geometry> quad = new osg::Geometry;

    // Bottom-right corner, 512x512 pixels
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back(osg::Vec3(0.7f, 0.0f, 0.0f));
    vertices->push_back(osg::Vec3(1.0f, 0.0f, 0.0f));
    vertices->push_back(osg::Vec3(1.0f, 0.3f, 0.0f));
    vertices->push_back(osg::Vec3(0.7f, 0.3f, 0.0f));

    osg::ref_ptr<osg::Vec2Array> texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2(0.0f, 0.0f));
    texCoords->push_back(osg::Vec2(1.0f, 0.0f));
    texCoords->push_back(osg::Vec2(1.0f, 1.0f));
    texCoords->push_back(osg::Vec2(0.0f, 1.0f));

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    colors->push_back(osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));

    quad->setVertexArray(vertices);
    quad->setTexCoordArray(0, texCoords);
    quad->setColorArray(colors, osg::Array::BIND_OVERALL);
    quad->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, 4));

    // Apply deformation texture
    osg::ref_ptr<osg::StateSet> ss = quad->getOrCreateStateSet();
    ss->setTextureAttributeAndModes(0, mManager->getDeformationTexture(), osg::StateAttribute::ON);
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    ss->setRenderBinDetails(1000, "RenderBin"); // Draw on top

    mTextureOverlayGeode->addDrawable(quad);
    mDebugGroup->addChild(mTextureOverlayGeode);

    Log(Debug::Info) << "SnowDeformationDebugger: Created texture overlay HUD";
}

void SnowDeformationDebugger::createFootprintMarkers()
{
    mFootprintMarkersGroup = new osg::Group;
    mFootprintMarkersGroup->setName("FootprintMarkers");
    mDebugGroup->addChild(mFootprintMarkersGroup);

    Log(Debug::Info) << "SnowDeformationDebugger: Created footprint markers group";
}

void SnowDeformationDebugger::createBoundsVisualization()
{
    mBoundsGeode = new osg::Geode;
    mBoundsGeode->setName("DeformationBounds");

    // Create a box outline showing the deformation texture coverage area
    float size = mManager->getWorldTextureSize();

    osg::ref_ptr<osg::Geometry> boundsBox = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;

    // Bottom square
    vertices->push_back(osg::Vec3(-size/2, -size/2, 0));
    vertices->push_back(osg::Vec3( size/2, -size/2, 0));
    vertices->push_back(osg::Vec3( size/2,  size/2, 0));
    vertices->push_back(osg::Vec3(-size/2,  size/2, 0));
    vertices->push_back(osg::Vec3(-size/2, -size/2, 0));

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    colors->push_back(osg::Vec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red

    boundsBox->setVertexArray(vertices);
    boundsBox->setColorArray(colors, osg::Array::BIND_OVERALL);
    boundsBox->addPrimitiveSet(new osg::DrawArrays(GL_LINE_STRIP, 0, 5));

    osg::ref_ptr<osg::StateSet> ss = boundsBox->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::LineWidth(3.0f), osg::StateAttribute::ON);
    ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    ss->setRenderBinDetails(100, "RenderBin");

    mBoundsGeode->addDrawable(boundsBox);
    mDebugGroup->addChild(mBoundsGeode);

    Log(Debug::Info) << "SnowDeformationDebugger: Created bounds visualization (size=" << size << ")";
}

void SnowDeformationDebugger::updateFootprintMarkers(const osg::Vec3f& cameraPos)
{
    // Clear old markers
    mFootprintMarkersGroup->removeChildren(0, mFootprintMarkersGroup->getNumChildren());

    // Add sphere for each active footprint
    // Note: We'd need to access mManager's footprint list (would need to add a getter)
    // For now, just log that we're trying
    static int counter = 0;
    if (++counter % 60 == 0)
    {
        Log(Debug::Info) << "SnowDeformationDebugger: Updating footprint markers (would need manager API)";
    }
}

void SnowDeformationDebugger::updateBoundsVisualization()
{
    if (!mBoundsGeode)
        return;

    // Update position to follow texture center
    osg::Vec2f center = mManager->getTextureCenter();
    osg::Matrix transform;
    transform.makeTranslate(osg::Vec3(center.x(), center.y(), 0.5f)); // Slight Z offset to be visible

    // Would need to use a MatrixTransform parent for the bounds geode
    // For now, this is a placeholder
}

} // namespace MWRender
