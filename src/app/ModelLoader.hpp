#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

namespace renderer {

struct ModelVertexData {
    QVector3D position = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D normal = QVector3D(0.0f, 1.0f, 0.0f);
    QVector2D uv = QVector2D(0.0f, 0.0f);
};

struct ModelMeshData {
    QVector<ModelVertexData> vertices;
    QVector<quint32> indices;
    QVector3D boundsMin = QVector3D(-0.5f, -0.5f, -0.5f);
    QVector3D boundsMax = QVector3D(0.5f, 0.5f, 0.5f);

    bool isValid() const {
        return !vertices.isEmpty() && !indices.isEmpty();
    }
};

struct ImportedMaterialData {
    QString texturePath;
    QByteArray embeddedTextureData;
    QVector3D tint = QVector3D(1.0f, 1.0f, 1.0f);
    bool flipVertically = false;

    bool hasTexture() const {
        return !texturePath.isEmpty() || !embeddedTextureData.isEmpty();
    }
};

struct ImportedSubMeshData {
    ModelMeshData mesh;
    int materialSlot = -1;
};

struct ModelImportData {
    QVector<ImportedSubMeshData> subMeshes;
    QVector<ImportedMaterialData> materials;
    QVector3D boundsMin = QVector3D(-0.5f, -0.5f, -0.5f);
    QVector3D boundsMax = QVector3D(0.5f, 0.5f, 0.5f);

    bool isValid() const {
        for (const ImportedSubMeshData& subMesh : subMeshes) {
            if (subMesh.mesh.isValid()) {
                return true;
            }
        }
        return false;
    }
};

class ModelLoader {
public:
    static ModelImportData importModel(const QString& path);
    static ModelMeshData load(const QString& path);
};

}  // namespace renderer