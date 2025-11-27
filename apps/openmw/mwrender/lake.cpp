#include "lake.hpp"
#include "water.hpp"
#include "cubemapreflection.hpp"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/PositionAttitudeTransform>
#include <osg/Depth>
#include <osg/BlendFunc>
#include <osg/Texture2D>
#include <osg/TextureCubeMap>
#include <osg/PolygonMode>
#include <osg/CullFace>

#include <osgUtil/CullVisitor>

#include <components/shader/shadermanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/sceneutil/depth.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/misc/constants.hpp>
#include <components/debug/debuglog.hpp>

#include "renderbin.hpp"
#include "vismask.hpp"

namespace MWRender
{

// Debug logging helper
namespace
{
    constexpr bool sLakeDebugLoggingEnabled = false;  // Set to true for debugging
    int sLakeFrameCounter = 0;
    constexpr int LOG_EVERY_N_FRAMES = 300;  // Log every 5 seconds at 60fps

    void logLake(const std::string& msg)
    {
        if (sLakeDebugLoggingEnabled)
            Log(Debug::Info) << "[Lake] " << msg;
    }

    void logLakeVerbose(const std::string& msg)
    {
        if (sLakeDebugLoggingEnabled && (sLakeFrameCounter % LOG_EVERY_N_FRAMES == 0))
            Log(Debug::Info) << "[Lake:Verbose] " << msg;
    }
}

// StateSetUpdater for per-frame SSR/cubemap texture binding
class LakeStateSetUpdater : public SceneUtil::StateSetUpdater
{
public:
    LakeStateSetUpdater(WaterManager* waterManager)
        : mWaterManager(waterManager)
        , mDebugMode(0)
    {
        logLake("LakeStateSetUpdater created");
    }

    void setDefaults(osg::StateSet* stateset) override
    {
        // Set texture unit uniforms
        stateset->addUniform(new osg::Uniform("sceneColorBuffer", 0));  // Scene color for SSR sampling
        stateset->addUniform(new osg::Uniform("environmentMap", 1));
        stateset->addUniform(new osg::Uniform("normalMap", 2));
        stateset->addUniform(new osg::Uniform("depthBuffer", 3));

        // Screen resolution for SSR sampling
        stateset->addUniform(new osg::Uniform("screenRes", osg::Vec2f(1920.f, 1080.f)));

        // Near/far for depth linearization
        stateset->addUniform(new osg::Uniform("near", 1.0f));
        stateset->addUniform(new osg::Uniform("far", 300000.0f));

        // Debug mode uniform: 0=normal, 1=solid color, 2=normals, 3=depth, 4=SSR only, 5=cubemap only
        stateset->addUniform(new osg::Uniform("debugMode", mDebugMode));

        // View/projection matrices for proper reflection calculations
        stateset->addUniform(new osg::Uniform("viewMatrix", osg::Matrixf()));
        stateset->addUniform(new osg::Uniform("projMatrix", osg::Matrixf()));
        stateset->addUniform(new osg::Uniform("invProjMatrix", osg::Matrixf()));

        // Camera position in world space
        stateset->addUniform(new osg::Uniform("cameraPos", osg::Vec3f(0, 0, 0)));
        
        // OG Water Shader Uniforms
        stateset->addUniform(new osg::Uniform("osg_SimulationTime", 0.0f));
        stateset->addUniform(new osg::Uniform("rainIntensity", 0.0f));
        stateset->addUniform(new osg::Uniform("enableRainRipples", false));
        stateset->addUniform(new osg::Uniform("playerPos", osg::Vec3f(0,0,0))); // Needed for ripples
        
        // SSR Mix Strength (0.0 = full cubemap, 1.0 = full SSR where confident)
        stateset->addUniform(new osg::Uniform("ssrMixStrength", 0.7f));

        logLake("LakeStateSetUpdater defaults set - texture units: SSR=0, Cubemap=1, Normal=2");
    }

    void apply(osg::StateSet* stateset, osg::NodeVisitor* nv) override
    {
        if (!mWaterManager)
        {
            if (sLakeFrameCounter == 0)
                logLake("WARNING: LakeStateSetUpdater has no WaterManager!");
            return;
        }

        osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(nv);
        if (!cv)
            return;

        sLakeFrameCounter++;
        bool shouldLog = (sLakeFrameCounter % LOG_EVERY_N_FRAMES == 0);

        // Get camera matrices
        osg::Camera* camera = cv->getCurrentCamera();
        if (camera)
        {
            osg::Matrixf viewMatrix = camera->getViewMatrix();
            osg::Matrixf projMatrix = camera->getProjectionMatrix();
            osg::Matrixf invViewMatrix = osg::Matrixf::inverse(viewMatrix);
            osg::Matrixf invProjMatrix = osg::Matrixf::inverse(projMatrix);

            osg::Uniform* viewUniform = stateset->getUniform("viewMatrix");
            osg::Uniform* projUniform = stateset->getUniform("projMatrix");
            osg::Uniform* invViewUniform = stateset->getUniform("invViewMatrix");
            osg::Uniform* invProjUniform = stateset->getUniform("invProjMatrix");
            osg::Uniform* camPosUniform = stateset->getUniform("cameraPos");

            if (viewUniform) viewUniform->set(viewMatrix);
            if (projUniform) projUniform->set(projMatrix);
            if (invViewUniform) invViewUniform->set(invViewMatrix);
            if (invProjUniform) invProjUniform->set(invProjMatrix);

            // Get camera position in world space
            // Since the Lake node is in world space, getEyeLocal() returns world position
            osg::Vec3f camPos = cv->getEyeLocal();
            if (camPosUniform) camPosUniform->set(camPos);

            // Update playerPos uniform (using camera pos as proxy for now)
            osg::Uniform* playerPosUniform = stateset->getUniform("playerPos");
            if (playerPosUniform) playerPosUniform->set(camPos);

            // Update simulation time
            osg::Uniform* timeUniform = stateset->getUniform("osg_SimulationTime");
            if (timeUniform && cv->getFrameStamp())
            {
                double time = cv->getFrameStamp()->getSimulationTime();
                timeUniform->set(static_cast<float>(time));
            }

            if (shouldLog)
            {
                logLakeVerbose("Camera pos: (" + std::to_string(camPos.x()) + ", "
                    + std::to_string(camPos.y()) + ", " + std::to_string(camPos.z()) + ")");
            }
        }

        // Get scene buffers for inline SSR raymarching
        osg::Texture2D* colorBuffer = mWaterManager->getSceneColorBuffer();
        osg::Texture2D* depthBuffer = mWaterManager->getSceneDepthBuffer();
        bool hasBuffers = false;

        if (colorBuffer && depthBuffer)
        {
            stateset->setTextureAttributeAndModes(0, colorBuffer, osg::StateAttribute::ON);
            stateset->setTextureAttributeAndModes(3, depthBuffer, osg::StateAttribute::ON);
            hasBuffers = true;

            if (shouldLog && sLakeFrameCounter < LOG_EVERY_N_FRAMES * 2)
            {
                logLake("Scene buffers bound: Color=" + std::to_string(colorBuffer->getTextureWidth()) + "x" + std::to_string(colorBuffer->getTextureHeight()));
            }
        }
        else if (shouldLog && sLakeFrameCounter < LOG_EVERY_N_FRAMES * 2)
        {
            logLake("WARNING: No scene buffers available for SSR raymarching");
        }

        // Get cubemap for approximate water position (use camera position as approximation)
        CubemapReflectionManager* cubemapMgr = mWaterManager->getCubemapManager();
        bool hasCubemap = false;
        if (cubemapMgr)
        {
            osg::Vec3f camPos = cv->getEyeLocal();
            osg::TextureCubeMap* cubemap = cubemapMgr->getCubemapForPosition(camPos);
            if (cubemap)
            {
                stateset->setTextureAttributeAndModes(1, cubemap, osg::StateAttribute::ON);
                hasCubemap = true;

                if (shouldLog && sLakeFrameCounter < LOG_EVERY_N_FRAMES * 2)
                {
                    // Log cubemap info on first few frames
                    logLake("Cubemap bound: size=" + std::to_string(cubemap->getTextureWidth())
                        + "x" + std::to_string(cubemap->getTextureHeight()));
                }
            }
            else if (shouldLog)
            {
                logLakeVerbose("WARNING: CubemapManager exists but getCubemapForPosition() returned null at pos ("
                    + std::to_string(camPos.x()) + ", " + std::to_string(camPos.y()) + ", " + std::to_string(camPos.z()) + ")");
            }
        }
        else if (shouldLog && sLakeFrameCounter < LOG_EVERY_N_FRAMES * 2)
        {
            logLake("WARNING: No CubemapReflectionManager available for lake reflections");
        }

        // Update screen resolution from viewport
        if (camera)
        {
            osg::Viewport* vp = camera->getViewport();
            if (vp)
            {
                osg::Uniform* screenResUniform = stateset->getUniform("screenRes");
                if (screenResUniform)
                    screenResUniform->set(osg::Vec2f(vp->width(), vp->height()));

                if (shouldLog && sLakeFrameCounter < LOG_EVERY_N_FRAMES * 2)
                {
                    logLake("Viewport: " + std::to_string((int)vp->width()) + "x" + std::to_string((int)vp->height()));
                }
            }
        }

        // Update debug mode
        osg::Uniform* debugUniform = stateset->getUniform("debugMode");
        if (debugUniform)
            debugUniform->set(mDebugMode);

        if (shouldLog)
        {
            logLakeVerbose("State update - Scene Buffers: " + std::string(hasBuffers ? "YES" : "NO")
                + ", Cubemap: " + std::string(hasCubemap ? "YES" : "NO")
                + ", DebugMode: " + std::to_string(mDebugMode)
                + ", Reversed-Z: " + std::string(SceneUtil::AutoDepth::isReversed() ? "YES" : "NO"));
        }
    }

    void setDebugMode(int mode) { mDebugMode = mode; }
    int getDebugMode() const { return mDebugMode; }

private:
    WaterManager* mWaterManager;
    int mDebugMode;

};

Lake::Lake(osg::Group* parent, Resource::ResourceSystem* resourceSystem)
    : mParent(parent)
    , mResourceSystem(resourceSystem)
    , mWaterManager(nullptr)
    , mEnabled(false)
{
    logLake("Lake constructor started");

    mRootNode = new osg::PositionAttitudeTransform;
    mRootNode->setName("LakeRoot");
    mRootNode->setNodeMask(Mask_Water);

    // Create shared water state set for all lake cells
    mWaterStateSet = createWaterStateSet();

    logLake("Lake constructor completed - root node created, state set initialized");
}

Lake::~Lake()
{
    if (mStateSetUpdater && mRootNode)
        mRootNode->removeCullCallback(mStateSetUpdater);
    removeFromScene(mParent);
}

void Lake::setWaterManager(WaterManager* waterManager)
{
    mWaterManager = waterManager;

    // Now that we have the water manager, we can create the state set updater
    if (mWaterManager && !mStateSetUpdater)
    {
        mStateSetUpdater = new LakeStateSetUpdater(mWaterManager);
        mRootNode->addCullCallback(mStateSetUpdater);
    }
}

void Lake::setEnabled(bool enabled)
{
    if (mEnabled == enabled)
        return;

    logLake("setEnabled: " + std::string(enabled ? "true" : "false") + " (was " + std::string(mEnabled ? "true" : "false") + ")");

    mEnabled = enabled;

    if (mEnabled)
    {
        addToScene(mParent);

        // Show all existing lake cells that have transforms ready
        // This handles the case where cells were loaded before the lake system was enabled
        int shownCount = 0;
        for (auto& pair : mCellWaters)
        {
            if (pair.second.transform && mRootNode && !mRootNode->containsNode(pair.second.transform))
            {
                mRootNode->addChild(pair.second.transform);
                shownCount++;
                logLake("  Retroactively showing lake cell (" + std::to_string(pair.second.gridX) + ", "
                          + std::to_string(pair.second.gridY) + ") at height " + std::to_string(pair.second.height));
            }
        }
        logLake("Lake system ENABLED: root node added to scene, retroactively showed " + std::to_string(shownCount) + " cells");
    }
    else
    {
        removeFromScene(mParent);
        logLake("Lake system DISABLED: root node removed from scene");
    }
}

void Lake::update(float dt, bool paused, const osg::Vec3f& cameraPos)
{
    // Cell centers are set once at creation time via the cellCenter uniform
    // No per-frame updates needed - the shader uses cellCenter + gl_Vertex for world position
}

void Lake::setHeight(float height)
{
    // Default height is not used for lakes as they are cell-based
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

    // Validate height
    if (!Units::isValidHeight(height))
    {
        logLake("ERROR: Invalid height " + std::to_string(height) + " for cell ("
                  + std::to_string(gridX) + ", " + std::to_string(gridY) + ")");
        logLake("  Height must be between " + std::to_string(Units::MIN_ALTITUDE)
                  + " and " + std::to_string(Units::MAX_ALTITUDE) + " units");
        return;
    }

    // Convert grid to world coordinates for validation
    float worldX, worldY;
    Units::gridToWorld(gridX, gridY, worldX, worldY);

    if (!Units::isValidWorldPos(worldX, worldY))
    {
        logLake("WARNING: Cell (" + std::to_string(gridX) + ", " + std::to_string(gridY)
                  + ") at world pos (" + std::to_string(worldX) + ", " + std::to_string(worldY)
                  + ") is outside normal Morrowind bounds");
    }

    logLake("addWaterCell: grid=(" + std::to_string(gridX) + ", " + std::to_string(gridY)
              + ") height=" + std::to_string(height) + " units ("
              + std::to_string(height * Units::FEET_PER_UNIT) + " feet, "
              + std::to_string(height * Units::METERS_PER_UNIT) + " meters)"
              + " enabled=" + std::string(mEnabled ? "true" : "false"));
    logLake("  World position: (" + std::to_string(worldX) + ", " + std::to_string(worldY) + ")");

    // Remove existing cell if present
    if (mCellWaters.count(key))
        removeWaterCell(gridX, gridY);

    // Create new cell water
    CellWater cell;
    cell.gridX = gridX;
    cell.gridY = gridY;
    cell.height = height;

    createCellGeometry(cell);

    mCellWaters[key] = cell;

    // Add to scene if enabled
    if (mEnabled && mRootNode && cell.transform)
    {
        mRootNode->addChild(cell.transform);
        logLake("  -> Cell added to scene (system enabled)");
    }
    else
    {
        logLake("  -> Cell created but NOT added to scene (system disabled or not ready)");
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
    // Convert world position to grid coordinates using Units helper
    int gridX, gridY;
    Units::worldToGrid(pos.x(), pos.y(), gridX, gridY);

    auto key = std::make_pair(gridX, gridY);
    auto it = mCellWaters.find(key);

    if (it != mCellWaters.end())
        return it->second.height;

    return -1000.0f;  // No water at this position
}

void Lake::createCellGeometry(CellWater& cell)
{
    const float cellSize = Units::CELL_SIZE_UNITS;  // 8192 MW units
    const float cellCenterX = cell.gridX * cellSize + cellSize * 0.5f;
    const float cellCenterY = cell.gridY * cellSize + cellSize * 0.5f;
    const float halfSize = cellSize * 0.5f;

    logLake("Creating geometry for cell (" + std::to_string(cell.gridX) + ", " + std::to_string(cell.gridY) + "):");
    logLake("  Cell size: " + std::to_string(cellSize) + " units ("
              + std::to_string(Units::CELL_SIZE_FEET) + " feet, "
              + std::to_string(Units::CELL_SIZE_METERS) + " meters)");
    logLake("  World position: (" + std::to_string(cellCenterX) + ", " + std::to_string(cellCenterY) + ", " + std::to_string(cell.height) + ")");
    logLake("  Quad extends: ±" + std::to_string(halfSize) + " units from center");

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

    // Add cell center uniform for world position calculation in shader
    // This avoids floating-point precision issues with large world coordinates
    osg::StateSet* ss = cell.transform->getOrCreateStateSet();
    ss->addUniform(new osg::Uniform("cellCenter", osg::Vec3f(cellCenterX, cellCenterY, cell.height)));
}

osg::ref_ptr<osg::StateSet> Lake::createWaterStateSet()
{
    logLake("Creating lake water state set");

    osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;

    // ============================================================
    // DEPTH RENDERING FIX: Match ocean.cpp configuration
    // ============================================================

    // Enable depth testing - critical for proper occlusion
    stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

    osg::ref_ptr<osg::Depth> depth = new osg::Depth;
    depth->setWriteMask(false);
    
    if (SceneUtil::AutoDepth::isReversed())
    {
        depth->setFunction(osg::Depth::GEQUAL);
        depth->setRange(1.0, 0.0);
    }
    else
    {
        depth->setFunction(osg::Depth::LEQUAL);
        depth->setRange(0.0, 1.0);
    }

    stateset->setAttributeAndModes(depth, osg::StateAttribute::ON);

    // Enable blending for transparency
    stateset->setMode(GL_BLEND, osg::StateAttribute::ON);

    osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc;
    blendFunc->setFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    stateset->setAttributeAndModes(blendFunc, osg::StateAttribute::ON);

    // Render in water bin (after opaque geometry)
    stateset->setRenderBinDetails(MWRender::RenderBin_Water, "RenderBin");

    logLake("Blend state: enabled, SRC_ALPHA/ONE_MINUS_SRC_ALPHA, bin=Water(9)");

    // Disable face culling so water is visible from above and below
    stateset->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);

    // Load water normal map texture (texture unit 2)
    constexpr VFS::Path::NormalizedView waterNormalImage("textures/omw/water_nm.png");
    osg::ref_ptr<osg::Texture2D> normalMap = new osg::Texture2D(
        mResourceSystem->getImageManager()->getImage(waterNormalImage));
    normalMap->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    normalMap->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
    mResourceSystem->getSceneManager()->applyFilterSettings(normalMap);
    stateset->setTextureAttributeAndModes(2, normalMap, osg::StateAttribute::ON);

    logLake("Normal map loaded at texture unit 2");

    // Use ShaderManager to get lake shaders
    Shader::ShaderManager& shaderManager = mResourceSystem->getSceneManager()->getShaderManager();

    osg::ref_ptr<osg::Program> program = new osg::Program;
    program->setName("LakeShader");

    // Use lake shaders with SSR+cubemap support
    auto vert = shaderManager.getShader("lake.vert", {}, osg::Shader::VERTEX);
    auto frag = shaderManager.getShader("lake.frag", {}, osg::Shader::FRAGMENT);

    if (vert)
    {
        program->addShader(vert);
        logLake("Vertex shader loaded: lake.vert");
    }
    else
    {
        logLake("WARNING: Failed to load lake.vert!");
    }

    if (frag)
    {
        program->addShader(frag);
        logLake("Fragment shader loaded: lake.frag");
    }
    else
    {
        logLake("WARNING: Failed to load lake.frag!");
    }

    stateset->setAttributeAndModes(program, osg::StateAttribute::ON);

    logLake("Lake water state set created successfully");

    return stateset;
}

void Lake::showWaterCell(int gridX, int gridY)
{
    auto key = std::make_pair(gridX, gridY);
    auto it = mCellWaters.find(key);

    if (it != mCellWaters.end())
    {
        logLake("showWaterCell: Found cell (" + std::to_string(gridX) + ", " + std::to_string(gridY)
                  + ") height=" + std::to_string(it->second.height));

        // Only add to scene if lake system is enabled and cell isn't already visible
        if (mEnabled && mRootNode && it->second.transform)
        {
            if (!mRootNode->containsNode(it->second.transform))
            {
                mRootNode->addChild(it->second.transform);
                logLake("  -> CELL SHOWN (added to scene)");
            }
            else
            {
                logLake("  -> Cell already visible");
            }
        }
        else
        {
            logLake("  -> Cell NOT shown (system disabled/not ready)");
        }
    }
    // Don't log when cell not found - this is normal for cells without lakes
}

void Lake::hideWaterCell(int gridX, int gridY)
{
    auto key = std::make_pair(gridX, gridY);
    auto it = mCellWaters.find(key);

    if (it != mCellWaters.end())
    {
        // Remove from scene if currently visible
        if (mRootNode && it->second.transform)
        {
            if (mRootNode->containsNode(it->second.transform))
            {
                mRootNode->removeChild(it->second.transform);
                logLake("hideWaterCell: Hidden cell (" + std::to_string(gridX) + ", " + std::to_string(gridY) + ")");
            }
        }
    }
}

void Lake::setDebugMode(int mode)
{
    LakeStateSetUpdater* updater = dynamic_cast<LakeStateSetUpdater*>(mStateSetUpdater.get());
    if (updater)
    {
        updater->setDebugMode(mode);
        logLake("Debug mode set to " + std::to_string(mode));
    }
}

int Lake::getDebugMode() const
{
    LakeStateSetUpdater* updater = dynamic_cast<LakeStateSetUpdater*>(mStateSetUpdater.get());
    if (updater)
        return updater->getDebugMode();
    return 0;
}

}
