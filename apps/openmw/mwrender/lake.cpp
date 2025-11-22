#include "lake.hpp"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/PositionAttitudeTransform>
#include <osg/Depth>

#include <components/shader/shadermanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/resource/scenemanager.hpp>

namespace MWRender
{

Lake::Lake(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
    : mParent(parent)
    , mResourceSystem(resourceSystem)
    , mHeight(0.f)
    , mEnabled(false)
{
    mRootNode = new osg::PositionAttitudeTransform;
    
    initGeometry();
    initShaders();
}

Lake::~Lake()
{
    removeFromScene(mParent);
}

void Lake::setEnabled(bool enabled)
{
    if (mEnabled == enabled)
        return;
    mEnabled = enabled;

    if (mEnabled)
        addToScene(mParent);
    else
        removeFromScene(mParent);
}

void Lake::update(float dt, bool paused, const osg::Vec3f& cameraPos)
{
    // Lake logic here (e.g. texture animation)
}

void Lake::setHeight(float height)
{
    mHeight = height;
    mRootNode->setPosition(osg::Vec3f(0.f, 0.f, mHeight));
}

bool Lake::isUnderwater(const osg::Vec3f& pos) const
{
    return pos.z() < mHeight;
}

void Lake::addToScene(osg::Group* parent)
{
    if (!parent->containsNode(mRootNode))
        parent->addChild(mRootNode);
}

void Lake::removeFromScene(osg::Group* parent)
{
    if (parent->containsNode(mRootNode))
        parent->removeChild(mRootNode);
}

void Lake::initGeometry()
{
    mWaterGeom = new osg::Geometry;
    
    // Create a large quad for the lake
    // In a real implementation, this might be tiled or use a grid
    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
    const float size = 100000.f; // Large enough to cover most cells
    verts->push_back(osg::Vec3f(-size, -size, 0.f));
    verts->push_back(osg::Vec3f(size, -size, 0.f));
    verts->push_back(osg::Vec3f(size, size, 0.f));
    verts->push_back(osg::Vec3f(-size, size, 0.f));
    
    mWaterGeom->setVertexArray(verts);
    
    osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
    texcoords->push_back(osg::Vec2f(0.f, 0.f));
    texcoords->push_back(osg::Vec2f(1.f, 0.f));
    texcoords->push_back(osg::Vec2f(1.f, 1.f));
    texcoords->push_back(osg::Vec2f(0.f, 1.f));
    
    mWaterGeom->setTexCoordArray(0, texcoords, osg::Array::BIND_PER_VERTEX);
    
    mWaterGeom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));
    
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(mWaterGeom);
    
    mRootNode->addChild(geode);
}

void Lake::initShaders()
{
    osg::StateSet* stateset = mRootNode->getOrCreateStateSet();
    
    // Enable blending
    stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
    
    // Use ShaderManager to get our lake shaders
    Shader::ShaderManager& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();
    
    osg::ref_ptr<osg::Program> program = new osg::Program;
    
    auto vert = shaderManager.getShader("lake.vert", {}, osg::Shader::VERTEX);
    auto frag = shaderManager.getShader("lake.frag", {}, osg::Shader::FRAGMENT);
    
    if (vert) program->addShader(vert);
    if (frag) program->addShader(frag);
    
    stateset->setAttributeAndModes(program, osg::StateAttribute::ON);
}

}
