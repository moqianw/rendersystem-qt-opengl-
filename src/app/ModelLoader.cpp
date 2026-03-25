#include "app/ModelLoader.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <assimp/cfileio.h>
#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <limits>
#include <stdexcept>

namespace {

using renderer::ImportedMaterialData;
using renderer::ImportedSubMeshData;
using renderer::ModelImportData;
using renderer::ModelMeshData;
using renderer::ModelVertexData;

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QVector3D minVec(const QVector3D& left, const QVector3D& right) {
    return QVector3D(
        qMin(left.x(), right.x()),
        qMin(left.y(), right.y()),
        qMin(left.z(), right.z()));
}

QVector3D maxVec(const QVector3D& left, const QVector3D& right) {
    return QVector3D(
        qMax(left.x(), right.x()),
        qMax(left.y(), right.y()),
        qMax(left.z(), right.z()));
}

void updateBounds(ModelMeshData* mesh, const QVector3D& position) {
    mesh->boundsMin = minVec(mesh->boundsMin, position);
    mesh->boundsMax = maxVec(mesh->boundsMax, position);
}

void recomputeNormals(ModelMeshData* mesh) {
    if (!mesh || mesh->indices.size() < 3) {
        return;
    }

    for (ModelVertexData& vertex : mesh->vertices) {
        vertex.normal = QVector3D(0.0f, 0.0f, 0.0f);
    }

    for (int index = 0; index + 2 < mesh->indices.size(); index += 3) {
        const quint32 ia = mesh->indices.at(index);
        const quint32 ib = mesh->indices.at(index + 1);
        const quint32 ic = mesh->indices.at(index + 2);
        if (ia >= static_cast<quint32>(mesh->vertices.size()) ||
            ib >= static_cast<quint32>(mesh->vertices.size()) ||
            ic >= static_cast<quint32>(mesh->vertices.size())) {
            continue;
        }

        const QVector3D ab = mesh->vertices.at(ib).position - mesh->vertices.at(ia).position;
        const QVector3D ac = mesh->vertices.at(ic).position - mesh->vertices.at(ia).position;
        const QVector3D normal = QVector3D::crossProduct(ab, ac).normalized();
        mesh->vertices[ia].normal += normal;
        mesh->vertices[ib].normal += normal;
        mesh->vertices[ic].normal += normal;
    }

    for (ModelVertexData& vertex : mesh->vertices) {
        if (vertex.normal.lengthSquared() <= 1e-6f) {
            vertex.normal = QVector3D(0.0f, 1.0f, 0.0f);
        } else {
            vertex.normal.normalize();
        }
    }
}

QString decodeAssimpPath(const char* path) {
    if (!path) {
        return {};
    }

    const QString utf8Path = QString::fromUtf8(path);
    if (!utf8Path.contains(QChar::ReplacementCharacter)) {
        return utf8Path;
    }
    return QString::fromLocal8Bit(path);
}

QString assimpErrorString() {
    const char* message = aiGetErrorString();
    if (!message || !*message) {
        return QStringLiteral("unknown assimp error");
    }
    return QString::fromUtf8(message);
}

QString resolveTexturePath(const QFileInfo& modelFileInfo, const QString& textureReference) {
    if (textureReference.isEmpty()) {
        return {};
    }

    const QString normalizedReference = QDir::fromNativeSeparators(textureReference);
    const QFileInfo textureInfo(normalizedReference);
    if (textureInfo.isAbsolute()) {
        return textureInfo.absoluteFilePath();
    }

    return QFileInfo(modelFileInfo.dir(), normalizedReference).absoluteFilePath();
}

QByteArray encodeImageToPng(const QImage& image) {
    if (image.isNull()) {
        return {};
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    if (!image.save(&buffer, "PNG")) {
        return {};
    }
    return encoded;
}

QByteArray extractEmbeddedTextureData(const aiScene* scene, const aiString& textureReference) {
    if (!scene) {
        return {};
    }

    const aiTexture* texture = aiGetEmbeddedTexture(scene, textureReference.C_Str());
    if (!texture || !texture->pcData) {
        return {};
    }

    if (texture->mHeight == 0) {
        return QByteArray(reinterpret_cast<const char*>(texture->pcData), static_cast<int>(texture->mWidth));
    }

    QImage image(
        static_cast<int>(texture->mWidth),
        static_cast<int>(texture->mHeight),
        QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const aiTexel& texel = texture->pcData[(y * image.width()) + x];
            image.setPixelColor(x, y, QColor(texel.r, texel.g, texel.b, texel.a));
        }
    }
    return encodeImageToPng(image);
}

ImportedMaterialData extractMaterialData(
    const aiScene* scene,
    const QFileInfo& modelFileInfo,
    const aiMaterial* sourceMaterial) {
    ImportedMaterialData material;
    material.flipVertically = false;

    if (!scene || !sourceMaterial) {
        return material;
    }

    aiColor4D color;
    if (aiGetMaterialColor(sourceMaterial, AI_MATKEY_BASE_COLOR, &color) == AI_SUCCESS ||
        aiGetMaterialColor(sourceMaterial, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS) {
        material.tint = QVector3D(color.r, color.g, color.b);
    }

    aiString textureReference;
    aiReturn textureResult = aiGetMaterialTexture(
        sourceMaterial,
        aiTextureType_BASE_COLOR,
        0,
        &textureReference,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);
    if (textureResult != AI_SUCCESS) {
        textureResult = aiGetMaterialTexture(
            sourceMaterial,
            aiTextureType_DIFFUSE,
            0,
            &textureReference,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
    }

    if (textureResult == AI_SUCCESS) {
        const QString texturePath = decodeAssimpPath(textureReference.C_Str());
        if (texturePath.startsWith(QLatin1Char('*'))) {
            material.embeddedTextureData = extractEmbeddedTextureData(scene, textureReference);
        } else {
            material.texturePath = resolveTexturePath(modelFileInfo, texturePath);
        }
    }

    return material;
}

ModelMeshData buildMeshData(const aiMesh* sourceMesh) {
    ModelMeshData mesh;
    mesh.boundsMin = QVector3D(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    mesh.boundsMax = QVector3D(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max());

    if (!sourceMesh || sourceMesh->mNumVertices == 0) {
        return mesh;
    }

    bool hasNormals = false;
    mesh.vertices.reserve(static_cast<int>(sourceMesh->mNumVertices));
    for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh->mNumVertices; ++vertexIndex) {
        ModelVertexData vertex;
        const aiVector3D& position = sourceMesh->mVertices[vertexIndex];
        vertex.position = QVector3D(position.x, position.y, position.z);

        if (sourceMesh->mNormals) {
            const aiVector3D& normal = sourceMesh->mNormals[vertexIndex];
            vertex.normal = QVector3D(normal.x, normal.y, normal.z);
            hasNormals = hasNormals || vertex.normal.lengthSquared() > 1e-6f;
        }
        if (sourceMesh->mTextureCoords[0]) {
            const aiVector3D& uv = sourceMesh->mTextureCoords[0][vertexIndex];
            vertex.uv = QVector2D(uv.x, uv.y);
        }

        mesh.vertices.append(vertex);
        updateBounds(&mesh, vertex.position);
    }

    for (unsigned int faceIndex = 0; faceIndex < sourceMesh->mNumFaces; ++faceIndex) {
        const aiFace& face = sourceMesh->mFaces[faceIndex];
        if (face.mNumIndices < 3 || !face.mIndices) {
            continue;
        }

        for (unsigned int corner = 1; corner + 1 < face.mNumIndices; ++corner) {
            mesh.indices.append(static_cast<quint32>(face.mIndices[0]));
            mesh.indices.append(static_cast<quint32>(face.mIndices[corner]));
            mesh.indices.append(static_cast<quint32>(face.mIndices[corner + 1]));
        }
    }

    if (!hasNormals) {
        recomputeNormals(&mesh);
    }

    return mesh;
}

struct QtAssimpFileHandle {
    QFile file;

    explicit QtAssimpFileHandle(const QString& path)
        : file(path) {}
};

struct QtAssimpFileSystemContext {
    QString baseDirectory;
};

size_t qtAssimpReadProc(aiFile* file, char* buffer, size_t size, size_t count) {
    if (!file || !buffer || size == 0 || count == 0) {
        return 0;
    }

    auto* handle = reinterpret_cast<QtAssimpFileHandle*>(file->UserData);
    const qint64 bytesRequested = static_cast<qint64>(size * count);
    const qint64 bytesRead = handle->file.read(buffer, bytesRequested);
    if (bytesRead <= 0) {
        return 0;
    }
    return static_cast<size_t>(bytesRead) / size;
}

size_t qtAssimpWriteProc(aiFile*, const char*, size_t, size_t) {
    return 0;
}

size_t qtAssimpTellProc(aiFile* file) {
    if (!file) {
        return 0;
    }

    auto* handle = reinterpret_cast<QtAssimpFileHandle*>(file->UserData);
    return static_cast<size_t>(qMax<qint64>(0, handle->file.pos()));
}

size_t qtAssimpFileSizeProc(aiFile* file) {
    if (!file) {
        return 0;
    }

    auto* handle = reinterpret_cast<QtAssimpFileHandle*>(file->UserData);
    return static_cast<size_t>(qMax<qint64>(0, handle->file.size()));
}

aiReturn qtAssimpSeekProc(aiFile* file, size_t offset, aiOrigin origin) {
    if (!file) {
        return aiReturn_FAILURE;
    }

    auto* handle = reinterpret_cast<QtAssimpFileHandle*>(file->UserData);
    qint64 target = 0;
    switch (origin) {
    case aiOrigin_SET:
        target = static_cast<qint64>(offset);
        break;
    case aiOrigin_CUR:
        target = handle->file.pos() + static_cast<qint64>(offset);
        break;
    case aiOrigin_END:
        target = handle->file.size() + static_cast<qint64>(offset);
        break;
    default:
        return aiReturn_FAILURE;
    }

    if (target < 0) {
        return aiReturn_FAILURE;
    }
    return handle->file.seek(target) ? aiReturn_SUCCESS : aiReturn_FAILURE;
}

void qtAssimpFlushProc(aiFile*) {}

aiFile* qtAssimpOpenProc(aiFileIO* fileIo, const char* path, const char*) {
    if (!fileIo || !path) {
        return nullptr;
    }

    auto* context = reinterpret_cast<QtAssimpFileSystemContext*>(fileIo->UserData);
    const QString requestedPath = QDir::fromNativeSeparators(decodeAssimpPath(path));
    if (requestedPath.isEmpty()) {
        return nullptr;
    }

    QString resolvedPath = requestedPath;
    const QFileInfo requestedInfo(requestedPath);
    if (!requestedInfo.isAbsolute()) {
        resolvedPath = QDir(context->baseDirectory).filePath(requestedPath);
    }
    resolvedPath = QDir::cleanPath(resolvedPath);

    auto* handle = new QtAssimpFileHandle(resolvedPath);
    if (!handle->file.open(QIODevice::ReadOnly)) {
        delete handle;
        return nullptr;
    }

    auto* wrapper = new aiFile();
    wrapper->ReadProc = &qtAssimpReadProc;
    wrapper->WriteProc = &qtAssimpWriteProc;
    wrapper->TellProc = &qtAssimpTellProc;
    wrapper->FileSizeProc = &qtAssimpFileSizeProc;
    wrapper->SeekProc = &qtAssimpSeekProc;
    wrapper->FlushProc = &qtAssimpFlushProc;
    wrapper->UserData = reinterpret_cast<aiUserData>(handle);
    return wrapper;
}

void qtAssimpCloseProc(aiFileIO*, aiFile* file) {
    if (!file) {
        return;
    }

    delete reinterpret_cast<QtAssimpFileHandle*>(file->UserData);
    delete file;
}

struct AssimpSceneHandle {
    const aiScene* scene = nullptr;

    ~AssimpSceneHandle() {
        if (scene) {
            aiReleaseImport(scene);
        }
    }
};

ModelImportData importAssimpModel(const QString& path) {
    QtAssimpFileSystemContext context{QFileInfo(path).absolutePath()};
    aiFileIO fileIo{};
    fileIo.OpenProc = &qtAssimpOpenProc;
    fileIo.CloseProc = &qtAssimpCloseProc;
    fileIo.UserData = reinterpret_cast<aiUserData>(&context);

    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_PreTransformVertices |
        aiProcess_SortByPType |
        aiProcess_FlipUVs;

    const QString normalizedPath = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    const QByteArray pathBytes = normalizedPath.toUtf8();

    AssimpSceneHandle importedScene;
    importedScene.scene = aiImportFileEx(pathBytes.constData(), flags, &fileIo);
    if (!importedScene.scene) {
        fail(QStringLiteral("failed to import model: %1\n%2").arg(path, assimpErrorString()));
    }
    if (importedScene.scene->mNumMeshes == 0 || !importedScene.scene->mMeshes) {
        fail(QStringLiteral("imported model contains no meshes: %1").arg(path));
    }

    ModelImportData imported;
    if (importedScene.scene->mNumMaterials > 0 && importedScene.scene->mMaterials) {
        imported.materials.reserve(static_cast<int>(importedScene.scene->mNumMaterials));
        for (unsigned int materialIndex = 0; materialIndex < importedScene.scene->mNumMaterials; ++materialIndex) {
            imported.materials.append(extractMaterialData(
                importedScene.scene,
                QFileInfo(path),
                importedScene.scene->mMaterials[materialIndex]));
        }
    }

    imported.boundsMin = QVector3D(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    imported.boundsMax = QVector3D(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max());

    for (unsigned int meshIndex = 0; meshIndex < importedScene.scene->mNumMeshes; ++meshIndex) {
        const aiMesh* source = importedScene.scene->mMeshes[meshIndex];
        ImportedSubMeshData subMesh;
        subMesh.materialSlot =
            source && source->mMaterialIndex < imported.materials.size()
                ? static_cast<int>(source->mMaterialIndex)
                : -1;
        subMesh.mesh = buildMeshData(source);
        if (!subMesh.mesh.isValid()) {
            continue;
        }

        imported.boundsMin = minVec(imported.boundsMin, subMesh.mesh.boundsMin);
        imported.boundsMax = maxVec(imported.boundsMax, subMesh.mesh.boundsMax);
        imported.subMeshes.append(subMesh);
    }

    if (!imported.isValid()) {
        fail(QStringLiteral("imported model does not contain renderable triangles: %1").arg(path));
    }

    return imported;
}

ModelMeshData mergeImportData(const ModelImportData& imported) {
    ModelMeshData merged;
    merged.boundsMin = QVector3D(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    merged.boundsMax = QVector3D(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max());

    for (const ImportedSubMeshData& subMesh : imported.subMeshes) {
        if (!subMesh.mesh.isValid()) {
            continue;
        }

        const quint32 baseVertex = static_cast<quint32>(merged.vertices.size());
        merged.vertices += subMesh.mesh.vertices;
        merged.boundsMin = minVec(merged.boundsMin, subMesh.mesh.boundsMin);
        merged.boundsMax = maxVec(merged.boundsMax, subMesh.mesh.boundsMax);
        for (quint32 index : subMesh.mesh.indices) {
            merged.indices.append(baseVertex + index);
        }
    }

    if (!merged.isValid()) {
        merged.boundsMin = QVector3D(-0.5f, -0.5f, -0.5f);
        merged.boundsMax = QVector3D(0.5f, 0.5f, 0.5f);
    }
    return merged;
}

}  // namespace

namespace renderer {

ModelImportData ModelLoader::importModel(const QString& path) {
    return importAssimpModel(path);
}

ModelMeshData ModelLoader::load(const QString& path) {
    return mergeImportData(importAssimpModel(path));
}

}  // namespace renderer