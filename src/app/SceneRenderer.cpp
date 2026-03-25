#include "app/SceneRenderer.hpp"

#include <QString>
#include <QtMath>
#include <QtGlobal>

#include <cmath>

namespace {

constexpr int kMaxLights = 16;

[[noreturn]] void fail(const QString& message) {
    qFatal("%s", qPrintable(message));
}

void require(bool condition, const QString& message) {
    if (!condition) {
        fail(message);
    }
}

QVector3D normalizedLightDirection(const renderer::LightConfig& light) {
    QVector3D direction = light.direction;
    if (direction.lengthSquared() <= 1e-6f) {
        direction = QVector3D(-0.45f, -1.0f, -0.3f);
    }
    return direction.normalized();
}

float sanitizedLightRange(const renderer::LightConfig& light) {
    return qMax(0.1f, light.range);
}

float sanitizedInnerConeDegrees(const renderer::LightConfig& light) {
    return qBound(0.1f, light.innerConeDegrees, 89.0f);
}

float sanitizedOuterConeDegrees(const renderer::LightConfig& light) {
    return qBound(sanitizedInnerConeDegrees(light), light.outerConeDegrees, 89.5f);
}

}  // namespace

namespace renderer {

void SceneRenderer::initialize(const PathResolver& pathResolver) {
    require(pathResolver != nullptr, QStringLiteral("scene renderer path resolver is required"));
    require(isInitialized() || initializeOpenGLFunctions(),
            QStringLiteral("failed to initialize scene renderer OpenGL functions"));

    program_.removeAllShaders();
    require(
        program_.addShaderFromSourceFile(
            QOpenGLShader::Vertex,
            pathResolver(QStringLiteral("assets/shaders/scene.vert"))),
        program_.log());
    require(
        program_.addShaderFromSourceFile(
            QOpenGLShader::Fragment,
            pathResolver(QStringLiteral("assets/shaders/scene.frag"))),
        program_.log());
    require(program_.link(), program_.log());
}

void SceneRenderer::cleanup() {
    program_.removeAllShaders();
}

void SceneRenderer::render(
    const CompiledRenderScene& scene,
    MeshHandle* cubeMesh,
    const QMatrix4x4& view,
    const QMatrix4x4& projection,
    const QVector3D& cameraPosition) {
    if (scene.items.isEmpty()) {
        return;
    }

    program_.bind();
    program_.setUniformValue(QStringLiteral("uView").toUtf8().constData(), view);
    program_.setUniformValue(QStringLiteral("uProjection").toUtf8().constData(), projection);
    program_.setUniformValue(QStringLiteral("uViewPos").toUtf8().constData(), cameraPosition);
    program_.setUniformValue(QStringLiteral("uMaterial.diffuseMap").toUtf8().constData(), 0);
    uploadLightsUniforms(scene.lights);

    for (const RenderItem& item : scene.items) {
        if (!item.visible || !item.material) {
            continue;
        }

        MeshHandle* mesh =
            item.geometrySource == RenderGeometrySource::Cube ? cubeMesh : item.mesh;
        if (!mesh || mesh->indexCount <= 0) {
            continue;
        }

        program_.setUniformValue(QStringLiteral("uModel").toUtf8().constData(), item.modelMatrix);
        program_.setUniformValue(QStringLiteral("uMaterial.tint").toUtf8().constData(), item.material->tint);

        if (item.material->texture) {
            item.material->texture->bind(0);
        }

        mesh->vao.bind();
        glDrawElements(mesh->primitiveType, mesh->indexCount, GL_UNSIGNED_INT, nullptr);
        mesh->vao.release();

        if (item.material->texture) {
            item.material->texture->release();
        }
    }

    program_.release();
}

void SceneRenderer::uploadLightsUniforms(const QVector<LightConfig>& lights) {
    const int lightCount = qMin(lights.size(), kMaxLights);
    program_.setUniformValue(QStringLiteral("uLightCount").toUtf8().constData(), lightCount);

    for (int lightIndex = 0; lightIndex < lightCount; ++lightIndex) {
        const LightConfig& light = lights.at(lightIndex);
        const QString uniformPrefix = QStringLiteral("uLights[%1].").arg(lightIndex);
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("type")).toUtf8().constData(),
            static_cast<int>(light.type));
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("position")).toUtf8().constData(),
            light.position);
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("direction")).toUtf8().constData(),
            normalizedLightDirection(light));
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("color")).toUtf8().constData(),
            light.color);
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("ambientStrength")).toUtf8().constData(),
            light.ambientStrength);
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("intensity")).toUtf8().constData(),
            light.intensity);
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("range")).toUtf8().constData(),
            sanitizedLightRange(light));
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("innerConeCos")).toUtf8().constData(),
            std::cos(qDegreesToRadians(sanitizedInnerConeDegrees(light))));
        program_.setUniformValue(
            (uniformPrefix + QStringLiteral("outerConeCos")).toUtf8().constData(),
            std::cos(qDegreesToRadians(sanitizedOuterConeDegrees(light))));
    }
}

}  // namespace renderer
