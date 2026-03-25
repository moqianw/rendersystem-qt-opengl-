#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLShaderProgram>
#include <QString>
#include <QVector3D>

#include <functional>

#include "app/RenderBackendTypes.hpp"

namespace renderer {

class SceneRenderer final : protected QOpenGLFunctions_4_5_Core {
public:
    using PathResolver = std::function<QString(const QString&)>;

    void initialize(const PathResolver& pathResolver);
    void cleanup();
    void render(
        const CompiledRenderScene& scene,
        MeshHandle* cubeMesh,
        const QMatrix4x4& view,
        const QMatrix4x4& projection,
        const QVector3D& cameraPosition);

private:
    void uploadLightsUniforms(const QVector<LightConfig>& lights);

    QOpenGLShaderProgram program_;
};

}  // namespace renderer
