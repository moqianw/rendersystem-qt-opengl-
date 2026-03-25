#include "app/RenderMeshUtils.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QString>
#include <QtGlobal>

#include <cstddef>

namespace {

[[noreturn]] void fail(const QString& message) {
    qFatal("%s", qPrintable(message));
}

void require(bool condition, const QString& message) {
    if (!condition) {
        fail(message);
    }
}

QOpenGLFunctions* glFunctions() {
    QOpenGLContext* context = QOpenGLContext::currentContext();
    require(context != nullptr, QStringLiteral("OpenGL context is not current"));
    auto* functions = context->functions();
    require(functions != nullptr, QStringLiteral("OpenGL functions unavailable"));
    functions->initializeOpenGLFunctions();
    return functions;
}

}  // namespace

namespace renderer::rendermesh {

void uploadMesh(
    MeshHandle* mesh,
    const std::vector<RenderVertex>& vertices,
    const std::vector<quint32>& indices,
    GLenum primitiveType) {
    if (!mesh) {
        return;
    }

    QOpenGLFunctions* functions = glFunctions();
    require(mesh->vao.create(), QStringLiteral("failed to create VAO"));
    require(mesh->vbo.create(), QStringLiteral("failed to create VBO"));
    require(mesh->ebo.create(), QStringLiteral("failed to create EBO"));

    mesh->vao.bind();
    mesh->vbo.bind();
    mesh->vbo.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(RenderVertex)));
    mesh->ebo.bind();
    mesh->ebo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(quint32)));

    functions->glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RenderVertex),
        reinterpret_cast<void*>(offsetof(RenderVertex, px)));
    functions->glEnableVertexAttribArray(0);
    functions->glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RenderVertex),
        reinterpret_cast<void*>(offsetof(RenderVertex, nx)));
    functions->glEnableVertexAttribArray(1);
    functions->glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RenderVertex),
        reinterpret_cast<void*>(offsetof(RenderVertex, u)));
    functions->glEnableVertexAttribArray(2);
    functions->glVertexAttribPointer(
        3,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(RenderVertex),
        reinterpret_cast<void*>(offsetof(RenderVertex, cr)));
    functions->glEnableVertexAttribArray(3);

    mesh->vao.release();
    mesh->vbo.release();
    mesh->ebo.release();

    mesh->indexCount = static_cast<int>(indices.size());
    mesh->primitiveType = primitiveType;
}

void destroyMesh(MeshHandle* mesh) {
    if (!mesh) {
        return;
    }

    mesh->vao.destroy();
    mesh->vbo.destroy();
    mesh->ebo.destroy();
    mesh->indexCount = 0;
}

}  // namespace renderer::rendermesh
