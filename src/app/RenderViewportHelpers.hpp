#pragma once

#include <QMatrix4x4>
#include <QVector3D>

#include "app/SceneConfig.hpp"

namespace renderer::viewport {

struct DebugRenderPlan {
    bool drawGrid = false;
    bool drawAxes = false;
    bool drawLightMarkers = false;
    bool drawObjectSelection = false;
    bool drawGizmo = false;

    bool hasAnyPass() const;
    bool requiresOverlayPass() const;
};

struct LightMarkerStyle {
    QMatrix4x4 modelMatrix;
    QVector3D tint = QVector3D(1.0f, 1.0f, 1.0f);
    float focusRadius = 1.0f;
};

DebugRenderPlan buildDebugRenderPlan(
    const DebugConfig& debug,
    bool hasObjectSelection,
    bool hasLightSelection,
    bool canRenderGizmo);

float effectiveGridStep(
    float baseStep,
    const QVector3D& cameraPosition,
    const QVector3D& cameraTarget);

QMatrix4x4 buildGridModelMatrix(float gridStep, const QVector3D& cameraTarget);

LightMarkerStyle buildLightMarkerStyle(const LightConfig& light, bool selected);

}  // namespace renderer::viewport
