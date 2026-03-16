#pragma once

#include <QSize>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

namespace renderer {

enum class GeometryType {
    Cube
};

struct WindowConfig {
    QSize size = QSize(1600, 900);
    QString title = QStringLiteral("Qt QOpenGL Renderer");
    QVector4D clearColor = QVector4D(0.93f, 0.95f, 0.98f, 1.0f);
    bool captureMouse = true;
};

struct CameraConfig {
    QVector3D position = QVector3D(8.5f, 6.0f, 14.0f);
    float yaw = -125.0f;
    float pitch = -18.0f;
    float fov = 45.0f;
    float nearClip = 0.1f;
    float farClip = 200.0f;
    float moveSpeed = 7.5f;
    float lookSpeed = 0.12f;
};

struct LightConfig {
    QVector3D position = QVector3D(10.0f, 12.0f, 8.0f);
    QVector3D color = QVector3D(1.0f, 0.97f, 0.92f);
    float ambientStrength = 0.18f;
    float intensity = 1.15f;
};

struct MaterialConfig {
    QString id;
    QString texturePath;
    QVector3D tint = QVector3D(1.0f, 1.0f, 1.0f);
};

struct RenderObjectConfig {
    QString name;
    GeometryType geometry = GeometryType::Cube;
    QString materialId;
    QVector3D position = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D rotationDegrees = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D scale = QVector3D(1.0f, 1.0f, 1.0f);
    bool visible = true;
};

struct DebugConfig {
    bool drawAxes = true;
    float axesLength = 8.0f;
    bool drawLightGizmo = true;
};

struct SceneConfig {
    WindowConfig window;
    CameraConfig camera;
    QVector<LightConfig> lights;
    QVector<MaterialConfig> materials;
    QVector<RenderObjectConfig> objects;
    DebugConfig debug;

    static SceneConfig loadFromFile(const QString& path);
};

}  // namespace renderer
