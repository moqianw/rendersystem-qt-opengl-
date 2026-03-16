#include "app/SceneConfig.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <stdexcept>

namespace {

QVector3D readVec3(const QJsonValue& value, const QVector3D& fallback) {
    const QJsonArray array = value.toArray();
    if (array.size() != 3) {
        return fallback;
    }

    return QVector3D(
        static_cast<float>(array.at(0).toDouble(fallback.x())),
        static_cast<float>(array.at(1).toDouble(fallback.y())),
        static_cast<float>(array.at(2).toDouble(fallback.z())));
}

QVector4D readVec4(const QJsonValue& value, const QVector4D& fallback) {
    const QJsonArray array = value.toArray();
    if (array.size() != 4) {
        return fallback;
    }

    return QVector4D(
        static_cast<float>(array.at(0).toDouble(fallback.x())),
        static_cast<float>(array.at(1).toDouble(fallback.y())),
        static_cast<float>(array.at(2).toDouble(fallback.z())),
        static_cast<float>(array.at(3).toDouble(fallback.w())));
}

renderer::GeometryType parseGeometryType(const QString& value) {
    if (value == QStringLiteral("cube")) {
        return renderer::GeometryType::Cube;
    }

    throw std::runtime_error(("unsupported geometry type: " + value).toStdString());
}

}  // namespace

namespace renderer {

SceneConfig SceneConfig::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(("failed to open scene config: " + path).toStdString());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        throw std::runtime_error(("failed to parse scene config: " + parseError.errorString()).toStdString());
    }

    const QJsonObject root = document.object();
    SceneConfig config;

    const QJsonObject window = root.value(QStringLiteral("window")).toObject();
    if (!window.isEmpty()) {
        config.window.title = window.value(QStringLiteral("title")).toString(config.window.title);
        config.window.size = QSize(
            window.value(QStringLiteral("width")).toInt(config.window.size.width()),
            window.value(QStringLiteral("height")).toInt(config.window.size.height()));
        config.window.captureMouse =
            window.value(QStringLiteral("captureMouse")).toBool(config.window.captureMouse);
        config.window.clearColor =
            readVec4(window.value(QStringLiteral("clearColor")), config.window.clearColor);
    }

    const QJsonObject camera = root.value(QStringLiteral("camera")).toObject();
    if (!camera.isEmpty()) {
        config.camera.position = readVec3(camera.value(QStringLiteral("position")), config.camera.position);
        config.camera.yaw =
            static_cast<float>(camera.value(QStringLiteral("yaw")).toDouble(config.camera.yaw));
        config.camera.pitch =
            static_cast<float>(camera.value(QStringLiteral("pitch")).toDouble(config.camera.pitch));
        config.camera.fov =
            static_cast<float>(camera.value(QStringLiteral("fov")).toDouble(config.camera.fov));
        config.camera.nearClip = static_cast<float>(
            camera.value(QStringLiteral("nearClip")).toDouble(config.camera.nearClip));
        config.camera.farClip =
            static_cast<float>(camera.value(QStringLiteral("farClip")).toDouble(config.camera.farClip));
        config.camera.moveSpeed = static_cast<float>(
            camera.value(QStringLiteral("moveSpeed")).toDouble(config.camera.moveSpeed));
        config.camera.lookSpeed = static_cast<float>(
            camera.value(QStringLiteral("lookSpeed")).toDouble(config.camera.lookSpeed));
    }

    const QJsonArray lights = root.value(QStringLiteral("lights")).toArray();
    for (const QJsonValue& lightValue : lights) {
        const QJsonObject light = lightValue.toObject();
        LightConfig parsed;
        parsed.position = readVec3(light.value(QStringLiteral("position")), parsed.position);
        parsed.color = readVec3(light.value(QStringLiteral("color")), parsed.color);
        parsed.ambientStrength = static_cast<float>(
            light.value(QStringLiteral("ambientStrength")).toDouble(parsed.ambientStrength));
        parsed.intensity =
            static_cast<float>(light.value(QStringLiteral("intensity")).toDouble(parsed.intensity));
        config.lights.append(parsed);
    }
    if (config.lights.isEmpty()) {
        config.lights.append(LightConfig{});
    }

    const QJsonArray materials = root.value(QStringLiteral("materials")).toArray();
    for (const QJsonValue& materialValue : materials) {
        const QJsonObject material = materialValue.toObject();
        MaterialConfig parsed;
        parsed.id = material.value(QStringLiteral("id")).toString();
        parsed.texturePath = material.value(QStringLiteral("texturePath")).toString();
        parsed.tint = readVec3(material.value(QStringLiteral("tint")), parsed.tint);
        config.materials.append(parsed);
    }

    const QJsonArray objects = root.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue& objectValue : objects) {
        const QJsonObject object = objectValue.toObject();
        RenderObjectConfig parsed;
        parsed.name = object.value(QStringLiteral("name")).toString(QStringLiteral("unnamed"));
        parsed.geometry =
            parseGeometryType(object.value(QStringLiteral("geometry")).toString(QStringLiteral("cube")));
        parsed.materialId = object.value(QStringLiteral("materialId")).toString();
        parsed.position = readVec3(object.value(QStringLiteral("position")), parsed.position);
        parsed.rotationDegrees =
            readVec3(object.value(QStringLiteral("rotationDegrees")), parsed.rotationDegrees);
        parsed.scale = readVec3(object.value(QStringLiteral("scale")), parsed.scale);
        parsed.visible = object.value(QStringLiteral("visible")).toBool(parsed.visible);
        config.objects.append(parsed);
    }

    const QJsonObject debug = root.value(QStringLiteral("debug")).toObject();
    if (!debug.isEmpty()) {
        config.debug.drawAxes =
            debug.value(QStringLiteral("drawAxes")).toBool(config.debug.drawAxes);
        config.debug.axesLength = static_cast<float>(
            debug.value(QStringLiteral("axesLength")).toDouble(config.debug.axesLength));
        config.debug.drawLightGizmo =
            debug.value(QStringLiteral("drawLightGizmo")).toBool(config.debug.drawLightGizmo);
    }

    return config;
}

}  // namespace renderer
