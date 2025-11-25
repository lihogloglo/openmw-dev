#include "lake.hpp"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/PositionAttitudeTransform>
#include <osg/Depth>
#include <osg/BlendFunc>

#include <components/shader/shadermanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/misc/constants.hpp>

#include "renderbin.hpp"

namespace MWRender
{

Lake::Lake(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
    : mParent(parent)
    , mResourceSystem(resourceSystem)
    , mDefaultHeight(0.f)
    , mEnabled(false)
{
    mRootNode = new osg::PositionAttitudeTransform;
    mRootNode->setName("LakeRoot");

    // Create shared water state set for all lake cells
    mWaterStateSet = createWaterStateSet();
}

Lake::~Lake()
{
    removeFromScene(mParent);
}

void Lake::setEnabled(bool enabled)
{
    if (mEnabled == enabled)
        return;

    std::cout << "Lake::setEnabled called: " << enabled << " (was " << mEnabled << ")" << std::endl;
    std::cout << "  Current cell count: " << mCellWaters.size() << std::endl;

    mEnabled = enabled;

    if (mEnabled)
    {
        addToScene(mParent);
        // Add all existing cells to the root node when enabled
        int addedCount = 0;
        for (auto& pair : mCellWaters)
        {
            if (pair.second.transform && !mRootNode->containsNode(pair.second.transform))
            {
                mRootNode->addChild(pair.second.transform);
                addedCount++;
                std::cout << "  Added cell (" << pair.second.gridX << ", " << pair.second.gridY
                          << ") to scene" << std::endl;
            }
        }
        std::cout << "Lake enabled: added " << addedCount << " cells to scene" << std::endl;
    }
    else
    {
        removeFromScene(mParent);
        std::cout << "Lake disabled: removed from scene" << std::endl;
    }
}

void Lake::update(float dt, bool paused, const osg::Vec3f& cameraPos)
{
    // Lake logic here (e.g. texture animation)
}

void Lake::setHeight(float height)
{
    mDefaultHeight = height;
    // Don't update existing cells - they have their own heights
}

bool Lake::isUnderwater(const osg::Vec3f& pos) const
{
    // Check if position is underwater in any lake cell
    float waterHeight = getWaterHeightAt(pos);
    return waterHeight > -999.0f && pos.z() < waterHeight;
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

void Lake::addWaterCell(int gridX, int gridY, float height)
{
    auto key = std::make_pair(gridX, gridY);

    std::cout << "Lake::addWaterCell called: gridX=" << gridX << " gridY=" << gridY
              << " height=" << height << " enabled=" << mEnabled << std::endl;

    // Remove existing cell if present
    if (mCellWaters.count(key))
        removeWaterCell(gridX, gridY);

    // Create new cell water
    CellWater cell;
    cell.gridX = gridX;
    cell.gridY = gridY;
    cell.height = height;

    createCellGeometry(cell);

    std::cout << "Lake cell geometry created, transform=" << (cell.transform != nullptr) << std::endl;

    mCellWaters[key] = cell;

    // Add to scene if enabled
    if (mEnabled && mRootNode && cell.transform)
    {
        mRootNode->addChild(cell.transform);
        std::cout << "Lake cell added to scene (enabled)" << std::endl;
    }
    else
    {
        std::cout << "Lake cell NOT added to scene: enabled=" << mEnabled
                  << " rootNode=" << (mRootNode != nullptr)
                  << " transform=" << (cell.transform != nullptr) << std::endl;
    }
}

void Lake::removeWaterCell(int gridX, int gridY)
{
    auto key = std::make_pair(gridX, gridY);
    auto it = mCellWaters.find(key);

    if (it != mCellWaters.end())
    {
        // Remove from scene
        if (mRootNode && it->second.transform)
            mRootNode->removeChild(it->second.transform);

        mCellWaters.erase(it);
    }
}

void Lake::clearAllCells()
{
    // Remove all cells from scene
    for (auto& pair : mCellWaters)
    {
        if (mRootNode && pair.second.transform)
            mRootNode->removeChild(pair.second.transform);
    }

    mCellWaters.clear();
}

float Lake::getWaterHeightAt(const osg::Vec3f& pos) const
{
    // Convert world position to grid coordinates
    const float cellSize = Constants::CellSizeInUnits;  // 8192 MW units
    int gridX = static_cast<int>(std::floor(pos.x() / cellSize));
    int gridY = static_cast<int>(std::floor(pos.y() / cellSize));

    auto key = std::make_pair(gridX, gridY);
    auto it = mCellWaters.find(key);

    if (it != mCellWaters.end())
        return it->second.height;

    return -1000.0f;  // No water at this position
}

void Lake::createCellGeometry(CellWater& cell)
{
    const float cellSize = Constants::CellSizeInUnits;  // 8192 MW units
    const float cellCenterX = cell.gridX * cellSize + cellSize * 0.5f;
    const float cellCenterY = cell.gridY * cellSize + cellSize * 0.5f;
    const float halfSize = cellSize * 0.5f;

    // Transform at cell center, water height
    cell.transform = new osg::PositionAttitudeTransform;
    cell.transform->setPosition(osg::Vec3f(cellCenterX, cellCenterY, cell.height));
    cell.transform->setName("LakeCell_" + std::to_string(cell.gridX) + "_" + std::to_string(cell.gridY));

    // Geometry (local coords, centered at origin)
    cell.geometry = new osg::Geometry;
    cell.geometry->setDataVariance(osg::Object::STATIC);

    osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array(4);
    (*verts)[0] = osg::Vec3f(-halfSize, -halfSize, 0.f);
    (*verts)[1] = osg::Vec3f( halfSize, -halfSize, 0.f);
    (*verts)[2] = osg::Vec3f( halfSize,  halfSize, 0.f);
    (*verts)[3] = osg::Vec3f(-halfSize,  halfSize, 0.f);
    cell.geometry->setVertexArray(verts);

    osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array(4);
    (*texcoords)[0] = osg::Vec2f(0.f, 0.f);
    (*texcoords)[1] = osg::Vec2f(1.f, 0.f);
    (*texcoords)[2] = osg::Vec2f(1.f, 1.f);
    (*texcoords)[3] = osg::Vec2f(0.f, 1.f);
    cell.geometry->setTexCoordArray(0, texcoords);

    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array(1);
    (*normals)[0] = osg::Vec3f(0.f, 0.f, 1.f);
    cell.geometry->setNormalArray(normals, osg::Array::BIND_OVERALL);

    cell.geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));

    // Apply shared water state set
    if (mWaterStateSet)
        cell.geometry->setStateSet(mWaterStateSet);

    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(cell.geometry);
    cell.transform->addChild(geode);
}

osg::ref_ptr<osg::StateSet> Lake::createWaterStateSet()
{
    osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;

    // Enable blending
    stateset->setMode(GL_BLEND, osg::StateAttribute::ON);

    osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc;
    blendFunc->setFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    stateset->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

    // Set render bin for water
    stateset->setRenderBinDetails(MWRender::RenderBin_Water, "RenderBin");

    // Depth settings
    osg::ref_ptr<osg::Depth> depth = new osg::Depth;
    depth->setWriteMask(false);  // Don't write to depth buffer
    stateset->setAttributeAndModes(depth, osg::StateAttribute::ON);

    // Use ShaderManager to get lake shaders
    Shader::ShaderManager& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();

    osg::ref_ptr<osg::Program> program = new osg::Program;

    // Use simple lake shaders (TODO: replace with proper water shader with SSR+cubemap)
    auto vert = shaderManager.getShader("lake.vert", {}, osg::Shader::VERTEX);
    auto frag = shaderManager.getShader("lake.frag", {}, osg::Shader::FRAGMENT);

    if (vert) program->addShader(vert);
    if (frag) program->addShader(frag);

    stateset->setAttributeAndModes(program, osg::StateAttribute::ON);

    return stateset;
}

}
