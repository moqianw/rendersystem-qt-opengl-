#include "app/RenderResourceManager.hpp"

#include <QByteArray>
#include <QColor>
#include <QDebug>
#include <QImage>
#include <QSet>

#include <exception>

#include "app/ModelLoader.hpp"
#include "app/RenderMeshUtils.hpp"

namespace {

renderer::MaterialResourcePtr createTextureBackedMaterial(const QImage& image, const QVector3D& tint) {
    auto material = std::make_shared<renderer::MaterialResource>();
    material->tint = tint;
    material->texture = std::make_shared<QOpenGLTexture>(image);
    material->texture->setWrapMode(QOpenGLTexture::Repeat);
    material->texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    material->texture->generateMipMaps();
    return material;
}

QImage fallbackWhiteImage() {
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(QColor(255, 255, 255, 255));
    return image;
}

renderer::MaterialResourcePtr buildFallbackMaterial() {
    return createTextureBackedMaterial(fallbackWhiteImage(), QVector3D(1.0f, 1.0f, 1.0f));
}

}  // namespace

namespace renderer {

void RenderResourceManager::sync(
    const SceneConfig& scene,
    bool reloadResources,
    const PathResolver& pathResolver) {
    if (!pathResolver) {
        return;
    }

    const bool needsRebuild = reloadResources || materials_.isEmpty();
    if (!needsRebuild) {
        return;
    }

    clear();
    rebuildMaterials(scene, pathResolver);
    rebuildModels(scene, pathResolver);
}

void RenderResourceManager::clear() {
    materials_.clear();

    for (auto it = models_.begin(); it != models_.end(); ++it) {
        if (!it.value()) {
            continue;
        }
        for (const auto& part : it.value()->parts) {
            if (part) {
                rendermesh::destroyMesh(&part->mesh);
            }
        }
    }
    models_.clear();
}

MaterialResourcePtr RenderResourceManager::material(const QString& materialId) const {
    const QString key = materialId.isEmpty() ? QStringLiteral("__default__") : materialId;
    const auto it = materials_.find(key);
    if (it != materials_.end()) {
        return it.value();
    }

    const auto fallbackIt = materials_.find(QStringLiteral("__default__"));
    return fallbackIt != materials_.end() ? fallbackIt.value() : MaterialResourcePtr{};
}

std::shared_ptr<const ModelResource> RenderResourceManager::model(const QString& sourcePath) const {
    const auto it = models_.find(sourcePath);
    if (it != models_.end()) {
        return it.value();
    }
    return {};
}

void RenderResourceManager::rebuildMaterials(
    const SceneConfig& scene,
    const PathResolver& pathResolver) {
    const QImage whiteImage = fallbackWhiteImage();
    materials_.insert(QStringLiteral("__default__"), buildFallbackMaterial());

    for (const MaterialConfig& materialConfig : scene.materials) {
        QImage image;
        if (!materialConfig.embeddedTextureBase64.isEmpty()) {
            const QByteArray encoded =
                QByteArray::fromBase64(materialConfig.embeddedTextureBase64.toUtf8());
            image.loadFromData(encoded);
        } else if (!materialConfig.texturePath.isEmpty()) {
            image = QImage(pathResolver(materialConfig.texturePath));
        }

        if (!image.isNull() && materialConfig.flipVertically) {
            image = image.mirrored();
        }
        if (image.isNull()) {
            image = whiteImage;
        } else {
            image = image.convertToFormat(QImage::Format_RGBA8888);
        }

        const QString id =
            materialConfig.id.isEmpty() ? QStringLiteral("__default__") : materialConfig.id;
        materials_.insert(id, createTextureBackedMaterial(image, materialConfig.tint));
    }
}

void RenderResourceManager::rebuildModels(
    const SceneConfig& scene,
    const PathResolver& pathResolver) {
    QSet<QString> initializedModels;
    for (const RenderObjectConfig& object : scene.objects) {
        if (object.geometry != GeometryType::Model || object.sourcePath.isEmpty()) {
            continue;
        }

        const QString key = resolvedModelKey(object.sourcePath, pathResolver);
        if (key.isEmpty() || initializedModels.contains(key)) {
            continue;
        }
        initializedModels.insert(key);

        auto runtime = std::make_shared<ModelResource>();
        try {
            const ModelImportData imported = ModelLoader::importModel(key);
            runtime->boundsMin = imported.boundsMin;
            runtime->boundsMax = imported.boundsMax;
            runtime->parts.reserve(imported.subMeshes.size());

            for (const ImportedSubMeshData& subMesh : imported.subMeshes) {
                if (!subMesh.mesh.isValid()) {
                    continue;
                }

                auto part = std::make_unique<ModelPartResource>();
                part->materialSlot = subMesh.materialSlot;

                std::vector<RenderVertex> vertices;
                vertices.reserve(static_cast<size_t>(subMesh.mesh.vertices.size()));
                for (const ModelVertexData& vertex : subMesh.mesh.vertices) {
                    vertices.push_back(RenderVertex{
                        vertex.position.x(),
                        vertex.position.y(),
                        vertex.position.z(),
                        vertex.normal.x(),
                        vertex.normal.y(),
                        vertex.normal.z(),
                        vertex.uv.x(),
                        vertex.uv.y(),
                        1.0f,
                        1.0f,
                        1.0f,
                    });
                }

                std::vector<quint32> indices;
                indices.reserve(static_cast<size_t>(subMesh.mesh.indices.size()));
                for (quint32 index : subMesh.mesh.indices) {
                    indices.push_back(index);
                }

                rendermesh::uploadMesh(&part->mesh, vertices, indices, GL_TRIANGLES);
                part->valid = part->mesh.indexCount > 0;
                runtime->parts.push_back(std::move(part));
            }

            for (const auto& part : runtime->parts) {
                runtime->valid = runtime->valid || (part && part->valid);
            }
        } catch (const std::exception& exception) {
            qWarning() << "Failed to load model" << key << exception.what();
        }

        models_.insert(key, runtime);
    }
}

QString RenderResourceManager::resolvedModelKey(
    const QString& sourcePath,
    const PathResolver& pathResolver) {
    return sourcePath.isEmpty() || !pathResolver ? QString() : pathResolver(sourcePath);
}

}  // namespace renderer
