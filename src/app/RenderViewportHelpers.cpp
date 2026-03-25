#include "app/RenderViewportHelpers.hpp"

#include <QQuaternion>
#include <QtMath>

#include <cmath>

namespace {

constexpr float kLightMarkerBaseScale = 0.25f;

QVector3D normalizedLightDirection(const renderer::LightConfig& light) {
    QVector3D direction = light.direction;
    if (direction.lengthSquared() <= 1e-6f) {
        direction = QVector3D(-0.45f, -1.0f, -0.3f);
    }
    return direction.normalized();
}

QVector3D lightMarkerScale(const renderer::LightConfig& light) {
    switch (light.type) {
    case renderer::LightType::Directional:
        return QVector3D(
            kLightMarkerBaseScale * 0.55f,
            kLightMarkerBaseScale * 0.55f,
            kLightMarkerBaseScale * 2.35f);
    case renderer::LightType::Spot:
        return QVector3D(
            kLightMarkerBaseScale * 0.78f,
            kLightMarkerBaseScale * 0.78f,
            kLightMarkerBaseScale * 1.75f);
    case renderer::LightType::Point:
        break;
    }

    return QVector3D(kLightMarkerBaseScale, kLightMarkerBaseScale, kLightMarkerBaseScale);
}

QVector3D selectedTint(const QVector3D& baseTint) {
    return QVector3D(
        qMin(2.2f, baseTint.x() + 0.45f),
        qMin(2.2f, baseTint.y() + 0.45f),
        qMin(2.2f, baseTint.z() + 0.45f));
}

}  // namespace

namespace renderer::viewport {

bool DebugRenderPlan::hasAnyPass() const {
    return drawGrid || drawAxes || drawLightMarkers || drawObjectSelection || drawGizmo;
}

bool DebugRenderPlan::requiresOverlayPass() const {
    return drawObjectSelection || drawGizmo;
}

DebugRenderPlan buildDebugRenderPlan(
    const DebugConfig& debug,
    bool hasObjectSelection,
    bool hasLightSelection,
    bool canRenderGizmo) {
    return DebugRenderPlan{
        debug.drawGrid,
        debug.drawAxes,
        debug.drawLightGizmo || hasLightSelection,
        hasObjectSelection,
        canRenderGizmo,
    };
}

float effectiveGridStep(
    float baseStep,
    const QVector3D& cameraPosition,
    const QVector3D& cameraTarget) {
    const float safeBaseStep = qMax(0.1f, baseStep);
    const float distanceMetric = qMax(
        safeBaseStep,
        qMax((cameraPosition - cameraTarget).length(), std::abs(cameraPosition.y())));
    float step = safeBaseStep;
    while ((step * 12.0f) < distanceMetric) {
        step *= 2.0f;
    }
    return step;
}

QMatrix4x4 buildGridModelMatrix(float gridStep, const QVector3D& cameraTarget) {
    const float step = qMax(0.1f, gridStep);
    const auto snapToGrid = [step](float value) {
        return std::round(value / step) * step;
    };

    QMatrix4x4 model;
    model.translate(snapToGrid(cameraTarget.x()), 0.0f, snapToGrid(cameraTarget.z()));
    model.scale(step);
    return model;
}

LightMarkerStyle buildLightMarkerStyle(const LightConfig& light, bool selected) {
    LightMarkerStyle style;
    const QVector3D scale = lightMarkerScale(light);

    QMatrix4x4 model;
    model.translate(light.position);
    if (light.type != LightType::Point) {
        model.rotate(QQuaternion::rotationTo(QVector3D(0.0f, 0.0f, -1.0f), normalizedLightDirection(light)));
    }
    model.scale(scale);

    style.modelMatrix = model;
    style.tint = selected ? selectedTint(light.color) : light.color;
    style.focusRadius = QVector3D(scale.x() * 0.5f, scale.y() * 0.5f, scale.z() * 0.5f).length();
    return style;
}

}  // namespace renderer::viewport
