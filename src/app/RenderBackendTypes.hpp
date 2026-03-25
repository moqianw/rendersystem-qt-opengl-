#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QVector>
#include <QVector3D>

#include <memory>
#include <vector>

#include "app/SceneConfig.hpp"

namespace renderer {

struct RenderVertex {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
    float cr;
    float cg;
    float cb;
};

struct MeshHandle {
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ebo{QOpenGLBuffer::IndexBuffer};
    int indexCount = 0;
    GLenum primitiveType = GL_TRIANGLES;
};

struct MaterialResource {
    QVector3D tint = QVector3D(1.0f, 1.0f, 1.0f);
    std::shared_ptr<QOpenGLTexture> texture;
};

using MaterialResourcePtr = std::shared_ptr<MaterialResource>;

struct ModelPartResource {
    MeshHandle mesh;
    int materialSlot = -1;
    bool valid = false;
};

struct ModelResource {
    std::vector<std::unique_ptr<ModelPartResource>> parts;
    QVector3D boundsMin = QVector3D(-0.5f, -0.5f, -0.5f);
    QVector3D boundsMax = QVector3D(0.5f, 0.5f, 0.5f);
    bool valid = false;
};

enum class RenderGeometrySource {
    Cube,
    ModelPart
};

struct RenderItem {
    int objectIndex = -1;
    bool visible = true;
    RenderGeometrySource geometrySource = RenderGeometrySource::Cube;
    MeshHandle* mesh = nullptr;
    MaterialResourcePtr material;
    QMatrix4x4 modelMatrix;
    QVector3D localBoundsMin = QVector3D(-0.5f, -0.5f, -0.5f);
    QVector3D localBoundsMax = QVector3D(0.5f, 0.5f, 0.5f);
};

struct RenderObjectInstance {
    int objectIndex = -1;
    bool visible = true;
    QMatrix4x4 worldTransform;
    QVector3D localBoundsMin = QVector3D(-0.5f, -0.5f, -0.5f);
    QVector3D localBoundsMax = QVector3D(0.5f, 0.5f, 0.5f);
};

struct CompiledRenderScene {
    QVector<RenderItem> items;
    QVector<RenderObjectInstance> objects;
    QVector<LightConfig> lights;

    void clear() {
        items.clear();
        objects.clear();
        lights.clear();
    }
};

}  // namespace renderer
