#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QVector>

#include <memory>
#include <vector>

#include "app/RenderMath.hpp"
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
    glm::vec3 tint = glm::vec3(1.0f, 1.0f, 1.0f);
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
    glm::vec3 boundsMin = glm::vec3(-0.5f, -0.5f, -0.5f);
    glm::vec3 boundsMax = glm::vec3(0.5f, 0.5f, 0.5f);
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
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    glm::vec3 localBoundsMin = glm::vec3(-0.5f, -0.5f, -0.5f);
    glm::vec3 localBoundsMax = glm::vec3(0.5f, 0.5f, 0.5f);
};

struct RenderObjectInstance {
    int objectIndex = -1;
    bool visible = true;
    glm::mat4 worldTransform = glm::mat4(1.0f);
    glm::vec3 localBoundsMin = glm::vec3(-0.5f, -0.5f, -0.5f);
    glm::vec3 localBoundsMax = glm::vec3(0.5f, 0.5f, 0.5f);
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
