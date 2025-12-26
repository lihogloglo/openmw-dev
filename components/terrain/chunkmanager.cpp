#include "chunkmanager.hpp"

#include <osg/Material>
#include <osg/Texture2D>

#include <osgUtil/IncrementalCompileOperation>

#include <components/debug/debuglog.hpp>
#include <components/esm/util.hpp>
#include <components/resource/objectcache.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/lightmanager.hpp>

#include <components/settings/values.hpp>

#include "compositemaprenderer.hpp"
#include "displacementmaprenderer.hpp"
#include "material.hpp"
#include "storage.hpp"
#include "terraindrawable.hpp"
#include "texturemanager.hpp"

namespace Terrain
{

    struct UpdateTextureFilteringFunctor
    {
        UpdateTextureFilteringFunctor(Resource::SceneManager* sceneMgr)
            : mSceneManager(sceneMgr)
        {
        }
        Resource::SceneManager* mSceneManager;

        void operator()(ChunkKey, osg::Object* obj)
        {
            TerrainDrawable* drawable = static_cast<TerrainDrawable*>(obj);
            CompositeMap* composite = drawable->getCompositeMap();
            if (composite && composite->mTexture)
                mSceneManager->applyFilterSettings(composite->mTexture);
        }
    };

    ChunkManager::ChunkManager(Storage* storage, Resource::SceneManager* sceneMgr, TextureManager* textureManager,
        CompositeMapRenderer* renderer, DisplacementMapRenderer* displacementRenderer,
        ESM::RefId worldspace, double expiryDelay)
        : GenericResourceManager<ChunkKey>(nullptr, expiryDelay)
        , QuadTreeWorld::ChunkManager(worldspace)
        , mStorage(storage)
        , mSceneManager(sceneMgr)
        , mTextureManager(textureManager)
        , mCompositeMapRenderer(renderer)
        , mDisplacementMapRenderer(displacementRenderer)
        , mNodeMask(0)
        , mCompositeMapSize(512)
        , mCompositeMapLevel(1.f)
        , mMaxCompGeometrySize(1.f)
    {
        mMultiPassRoot = new osg::StateSet;
        mMultiPassRoot->setRenderingHint(osg::StateSet::OPAQUE_BIN);
        osg::ref_ptr<osg::Material> material(new osg::Material);
        material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
        mMultiPassRoot->setAttributeAndModes(material, osg::StateAttribute::ON);
    }

    osg::ref_ptr<osg::Node> ChunkManager::getChunk(float size, const osg::Vec2f& center, unsigned char lod,
        unsigned int lodFlags, bool activeGrid, const osg::Vec3f& viewPoint, bool compile)
    {
        // Override lod with the vertexLodMod adjusted value.
        lod = static_cast<unsigned char>(lodFlags >> (4 * 4));

        const ChunkKey key{ .mCenter = center, .mLod = lod, .mLodFlags = lodFlags };
        if (osg::ref_ptr<osg::Object> obj = mCache->getRefFromObjectCache(key))
        {
            return static_cast<osg::Node*>(obj.get());
        }

        const TerrainDrawable* templateGeometry = nullptr;
        const TemplateKey templateKey{ .mCenter = center, .mLod = lod };
        const auto pair = mCache->lowerBound(templateKey);
        if (pair.has_value() && templateKey == TemplateKey{ .mCenter = pair->first.mCenter, .mLod = pair->first.mLod })
            templateGeometry = static_cast<const TerrainDrawable*>(pair->second.get());

        osg::ref_ptr<osg::Node> node = createChunk(size, center, lod, lodFlags, compile, templateGeometry, viewPoint);
        mCache->addEntryToObjectCache(key, node.get());
        return node;
    }

    void ChunkManager::updateTextureFiltering()
    {
        UpdateTextureFilteringFunctor f(mSceneManager);
        mCache->call(f);
    }

    void ChunkManager::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        Resource::reportStats("Terrain Chunk", frameNumber, mCache->getStats(), *stats);
    }

    void ChunkManager::clearCache()
    {
        GenericResourceManager<ChunkKey>::clearCache();
        mBufferCache.clearCache();
    }

    void ChunkManager::releaseGLObjects(osg::State* state)
    {
        GenericResourceManager<ChunkKey>::releaseGLObjects(state);
        mBufferCache.releaseGLObjects(state);
    }

    osg::ref_ptr<osg::Texture2D> ChunkManager::createCompositeMapRTT()
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setTextureWidth(mCompositeMapSize);
        texture->setTextureHeight(mCompositeMapSize);
        texture->setInternalFormat(GL_RGB);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        mSceneManager->applyFilterSettings(texture);

        return texture;
    }

    void ChunkManager::createCompositeMapGeometry(
        float chunkSize, const osg::Vec2f& chunkCenter, const osg::Vec4f& texCoords, CompositeMap& compositeMap)
    {
        if (chunkSize > mMaxCompGeometrySize)
        {
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x() + texCoords.z() / 2.f, texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(-chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(texCoords.x(), texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f), compositeMap);
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(texCoords.x() + texCoords.z() / 2.f, texCoords.y() + texCoords.w() / 2.f,
                    texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
            createCompositeMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(-chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x(), texCoords.y() + texCoords.w() / 2.f, texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
        }
        else
        {
            float left = texCoords.x() * 2.f - 1;
            float top = texCoords.y() * 2.f - 1;
            float width = texCoords.z() * 2.f;
            float height = texCoords.w() * 2.f;

            std::vector<osg::ref_ptr<osg::StateSet>> passes = createPasses(chunkSize, chunkCenter, true);
            for (std::vector<osg::ref_ptr<osg::StateSet>>::iterator it = passes.begin(); it != passes.end(); ++it)
            {
                osg::ref_ptr<osg::Geometry> geom = osg::createTexturedQuadGeometry(
                    osg::Vec3(left, top, 0), osg::Vec3(width, 0, 0), osg::Vec3(0, height, 0));
                geom->setUseDisplayList(
                    false); // don't bother making a display list for an object that is just rendered once.
                geom->setUseVertexBufferObjects(false);
                geom->setTexCoordArray(1, geom->getTexCoordArray(0), osg::Array::BIND_PER_VERTEX);

                geom->setStateSet(*it);

                compositeMap.mDrawables.emplace_back(geom);
            }
        }
    }

    osg::ref_ptr<osg::Texture2D> ChunkManager::createDisplacementMapRTT()
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        // Use same size as composite map for consistency
        texture->setTextureWidth(mCompositeMapSize);
        texture->setTextureHeight(mCompositeMapSize);
        // RG format: R = weighted height sum, G = weight sum
        texture->setInternalFormat(GL_RG16F);
        texture->setSourceFormat(GL_RG);
        texture->setSourceType(GL_FLOAT);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);

        return texture;
    }

    void ChunkManager::createDisplacementMapGeometry(
        float chunkSize, const osg::Vec2f& chunkCenter, const osg::Vec4f& texCoords, DisplacementMap& displacementMap)
    {
        if (chunkSize > mMaxCompGeometrySize)
        {
            createDisplacementMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x() + texCoords.z() / 2.f, texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f),
                displacementMap);
            createDisplacementMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(-chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(texCoords.x(), texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f), displacementMap);
            createDisplacementMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(texCoords.x() + texCoords.z() / 2.f, texCoords.y() + texCoords.w() / 2.f,
                    texCoords.z() / 2.f, texCoords.w() / 2.f),
                displacementMap);
            createDisplacementMapGeometry(chunkSize / 2.f, chunkCenter + osg::Vec2f(-chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x(), texCoords.y() + texCoords.w() / 2.f, texCoords.z() / 2.f, texCoords.w() / 2.f),
                displacementMap);
        }
        else
        {
            float left = texCoords.x() * 2.f - 1;
            float top = texCoords.y() * 2.f - 1;
            float width = texCoords.z() * 2.f;
            float height = texCoords.w() * 2.f;

            // Get layer info for displacement map passes
            std::vector<LayerInfo> layerList;
            std::vector<osg::ref_ptr<osg::Image>> blendmaps;
            mStorage->getBlendmaps(chunkSize, chunkCenter, blendmaps, layerList, mWorldspace);

            std::vector<osg::ref_ptr<osg::Texture2D>> blendmapTextures;
            for (const auto& image : blendmaps)
            {
                osg::ref_ptr<osg::Texture2D> texture(new osg::Texture2D);
                texture->setImage(image);
                texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
                texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
                texture->setResizeNonPowerOfTwoHint(false);
                blendmapTextures.push_back(texture);
            }

            float tileCount = mStorage->getTextureTileCount(chunkSize, mWorldspace);

            // Create displacement map passes using the material function
            // Pass chunkSize and chunkCenter for world-space consistent UV calculation
            auto passes = ::Terrain::createDisplacementMapPasses(
                mSceneManager, layerList, blendmapTextures, tileCount, chunkSize, chunkCenter, mTextureManager);

            for (auto& pass : passes)
            {
                osg::ref_ptr<osg::Geometry> geom = osg::createTexturedQuadGeometry(
                    osg::Vec3(left, top, 0), osg::Vec3(width, 0, 0), osg::Vec3(0, height, 0));
                geom->setUseDisplayList(false);
                geom->setUseVertexBufferObjects(false);
                geom->setTexCoordArray(1, geom->getTexCoordArray(0), osg::Array::BIND_PER_VERTEX);

                geom->setStateSet(pass);

                displacementMap.mDrawables.emplace_back(geom);
            }
        }
    }

    std::vector<osg::ref_ptr<osg::StateSet>> ChunkManager::createPasses(
        float chunkSize, const osg::Vec2f& chunkCenter, bool forCompositeMap)
    {
        std::vector<LayerInfo> layerList;
        std::vector<osg::ref_ptr<osg::Image>> blendmaps;
        mStorage->getBlendmaps(chunkSize, chunkCenter, blendmaps, layerList, mWorldspace);

        bool useShaders = mSceneManager->getForceShaders();
        if (!mSceneManager->getClampLighting())
            useShaders = true; // always use shaders when lighting is unclamped, this is to avoid lighting seams between
                               // a terrain chunk with normal maps and one without normal maps

        std::vector<TextureLayer> layers;
        {
            for (std::vector<LayerInfo>::const_iterator it = layerList.begin(); it != layerList.end(); ++it)
            {
                TextureLayer textureLayer;
                textureLayer.mParallax = it->mParallax;
                textureLayer.mSpecular = it->mSpecular;

                textureLayer.mDiffuseMap = mTextureManager->getTexture(it->mDiffuseMap);

                if (!forCompositeMap && !it->mNormalMap.empty())
                    textureLayer.mNormalMap = mTextureManager->getTexture(it->mNormalMap);

                if (it->requiresShaders())
                    useShaders = true;

                layers.push_back(textureLayer);
            }
        }

        if (forCompositeMap)
            useShaders = false;

        std::vector<osg::ref_ptr<osg::Texture2D>> blendmapTextures;
        for (std::vector<osg::ref_ptr<osg::Image>>::const_iterator it = blendmaps.begin(); it != blendmaps.end(); ++it)
        {
            osg::ref_ptr<osg::Texture2D> texture(new osg::Texture2D);
            texture->setImage(*it);
            texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            texture->setResizeNonPowerOfTwoHint(false);
            blendmapTextures.push_back(texture);
        }

        float tileCount = mStorage->getTextureTileCount(chunkSize, mWorldspace);

        // Use tessellation passes for non-composite chunks when tessellation is enabled in settings
        if (!forCompositeMap && Settings::terrain().mTessellation.get())
        {
            auto passes = ::Terrain::createTessellationPasses(
                mSceneManager, layers, blendmapTextures, tileCount, tileCount, ESM::isEsm4Ext(mWorldspace));
            if (!passes.empty())
                return passes;
            // Fall back to regular passes if tessellation shader compilation fails
            Log(Debug::Warning) << "Tessellation passes failed, falling back to regular shaders";
        }

        return ::Terrain::createPasses(
            useShaders, mSceneManager, layers, blendmapTextures, tileCount, tileCount, ESM::isEsm4Ext(mWorldspace));
    }

    osg::ref_ptr<osg::Node> ChunkManager::createChunk(float chunkSize, const osg::Vec2f& chunkCenter, unsigned char lod,
        unsigned int lodFlags, bool compile, const TerrainDrawable* templateGeometry, const osg::Vec3f& viewPoint)
    {
        osg::ref_ptr<TerrainDrawable> geometry(new TerrainDrawable);

        if (!templateGeometry)
        {
            osg::ref_ptr<osg::Vec3Array> positions(new osg::Vec3Array);
            osg::ref_ptr<osg::Vec3Array> normals(new osg::Vec3Array);
            osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray);
            colors->setNormalize(true);

            mStorage->fillVertexBuffers(lod, chunkSize, chunkCenter, mWorldspace, *positions, *normals, *colors);

            osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject);
            positions->setVertexBufferObject(vbo);
            normals->setVertexBufferObject(vbo);
            colors->setVertexBufferObject(vbo);

            geometry->setVertexArray(positions);
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        }
        else
        {
            // Unfortunately we need to copy vertex data because of poor coupling with VertexBufferObject.
            osg::ref_ptr<osg::Array> positions
                = static_cast<osg::Array*>(templateGeometry->getVertexArray()->clone(osg::CopyOp::DEEP_COPY_ALL));
            osg::ref_ptr<osg::Array> normals
                = static_cast<osg::Array*>(templateGeometry->getNormalArray()->clone(osg::CopyOp::DEEP_COPY_ALL));
            osg::ref_ptr<osg::Array> colors
                = static_cast<osg::Array*>(templateGeometry->getColorArray()->clone(osg::CopyOp::DEEP_COPY_ALL));

            osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject);
            positions->setVertexBufferObject(vbo);
            normals->setVertexBufferObject(vbo);
            colors->setVertexBufferObject(vbo);

            geometry->setVertexArray(positions);
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        }

        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);

        if (chunkSize <= 1.f)
            geometry->setLightListCallback(new SceneUtil::LightListCallback);

        unsigned int numVerts = (mStorage->getCellVertices(mWorldspace) - 1) * chunkSize / (1 << lod) + 1;

        // Determine if this chunk uses composite map (large/distant chunks)
        bool useCompositeMap = chunkSize >= mCompositeMapLevel;

        // Use patch index buffer for tessellation, but ONLY when:
        // 1. Not using composite map (tessellation only for nearby detailed chunks)
        // 2. Tessellation is enabled in settings
        bool useTessellation = !useCompositeMap && Settings::terrain().mTessellation.get();
        if (useTessellation)
            geometry->addPrimitiveSet(mBufferCache.getPatchIndexBuffer(numVerts, lodFlags));
        else
            geometry->addPrimitiveSet(mBufferCache.getIndexBuffer(numVerts, lodFlags));

        unsigned int numUvSets = useCompositeMap ? 1 : 2;

        geometry->setTexCoordArrayList(osg::Geometry::ArrayList(numUvSets, mBufferCache.getUVBuffer(numVerts)));

        geometry->createClusterCullingCallback();

        // Create a chunk-specific stateset that inherits from mMultiPassRoot
        osg::ref_ptr<osg::StateSet> chunkStateSet = new osg::StateSet(*mMultiPassRoot, osg::CopyOp::SHALLOW_COPY);

        // Add camera position uniform for tessellation LOD calculation
        if (useTessellation)
        {
            chunkStateSet->addUniform(new osg::Uniform("cameraPos", viewPoint));
        }

        geometry->setStateSet(chunkStateSet);

        if (templateGeometry)
        {
            if (templateGeometry->getCompositeMap())
            {
                geometry->setCompositeMap(templateGeometry->getCompositeMap());
                geometry->setCompositeMapRenderer(mCompositeMapRenderer);
            }
            if (templateGeometry->getDisplacementMap())
            {
                geometry->setDisplacementMap(templateGeometry->getDisplacementMap());
                geometry->setDisplacementMapRenderer(mDisplacementMapRenderer);
            }
            geometry->setPasses(templateGeometry->getPasses());
        }
        else
        {
            if (useCompositeMap)
            {
                osg::ref_ptr<CompositeMap> compositeMap = new CompositeMap;
                compositeMap->mTexture = createCompositeMapRTT();

                createCompositeMapGeometry(chunkSize, chunkCenter, osg::Vec4f(0, 0, 1, 1), *compositeMap);

                mCompositeMapRenderer->addCompositeMap(compositeMap.get(), false);

                geometry->setCompositeMap(compositeMap);
                geometry->setCompositeMapRenderer(mCompositeMapRenderer);

                TextureLayer layer;
                layer.mDiffuseMap = compositeMap->mTexture;
                layer.mParallax = false;
                layer.mSpecular = false;
                geometry->setPasses(::Terrain::createPasses(
                    mSceneManager->getForceShaders() || !mSceneManager->getClampLighting(), mSceneManager,
                    std::vector<TextureLayer>(1, layer), std::vector<osg::ref_ptr<osg::Texture2D>>(), 1.f, 1.f));
            }
            else
            {
                geometry->setPasses(createPasses(chunkSize, chunkCenter, false));

                // Create displacement map for tessellated terrain with heightmap displacement enabled
                // The displacement map pre-blends all layers' heights weighted by blend maps.
                // World-space UV offsets ensure seamless sampling across chunk boundaries.
                if (useTessellation && Settings::terrain().mHeightmapDisplacement.get() && mDisplacementMapRenderer)
                {
                    osg::ref_ptr<DisplacementMap> displacementMap = new DisplacementMap;
                    displacementMap->mTexture = createDisplacementMapRTT();

                    createDisplacementMapGeometry(chunkSize, chunkCenter, osg::Vec4f(0, 0, 1, 1), *displacementMap);

                    mDisplacementMapRenderer->addDisplacementMap(displacementMap.get(), false);

                    geometry->setDisplacementMap(displacementMap);
                    geometry->setDisplacementMapRenderer(mDisplacementMapRenderer);

                    // Add the displacement map texture to the chunk's stateset so all passes can access it
                    chunkStateSet->setTextureAttributeAndModes(3, displacementMap->mTexture, osg::StateAttribute::ON);
                    chunkStateSet->addUniform(new osg::Uniform("displacementMap", 3));
                }
            }
        }

        geometry->setupWaterBoundingBox(-1, chunkSize * mStorage->getCellWorldSize(mWorldspace) / numVerts);

        if (!templateGeometry && compile && mSceneManager->getIncrementalCompileOperation())
        {
            mSceneManager->getIncrementalCompileOperation()->add(geometry);
        }
        geometry->setNodeMask(mNodeMask);

        return geometry;
    }

}
