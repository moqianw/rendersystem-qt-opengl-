#include "app/SceneConfig.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

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
    if (value == QStringLiteral("model")) {
        return renderer::GeometryType::Model;
    }

    throw std::runtime_error(("unsupported geometry type: " + value).toStdString());
}

renderer::LightType parseLightType(const QString& value) {
    if (value == QStringLiteral("directional")) {
        return renderer::LightType::Directional;
    }
    if (value == QStringLiteral("point")) {
        return renderer::LightType::Point;
    }
    if (value == QStringLiteral("spot")) {
        return renderer::LightType::Spot;
    }

    throw std::runtime_error(("unsupported light type: " + value).toStdString());
}

QJsonArray writeVec3(const QVector3D& value) {
    return QJsonArray{value.x(), value.y(), value.z()};
}

QJsonArray writeVec4(const QVector4D& value) {
    return QJsonArray{value.x(), value.y(), value.z(), value.w()};
}

QString geometryTypeToString(renderer::GeometryType geometryType) {
    switch (geometryType) {
    case renderer::GeometryType::Cube:
        return QStringLiteral("cube");
    case renderer::GeometryType::Model:
        return QStringLiteral("model");
    }

    return QStringLiteral("cube");
}

QString lightTypeToString(renderer::LightType lightType) {
    switch (lightType) {
    case renderer::LightType::Directional:
        return QStringLiteral("directional");
    case renderer::LightType::Point:
        return QStringLiteral("point");
    case renderer::LightType::Spot:
        return QStringLiteral("spot");
    }

    return QStringLiteral("point");
}

QString normalizeStoredPath(const QString& path) {
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo fileInfo(path);
    const QString absolutePath = fileInfo.isAbsolute()
        ? fileInfo.absoluteFilePath()
        : QFileInfo(QDir(QCoreApplication::applicationDirPath()), path).absoluteFilePath();
    const QDir applicationDir(QCoreApplication::applicationDirPath());
    const QString relativePath = QDir::cleanPath(applicationDir.relativeFilePath(absolutePath));
    return QDir(relativePath).isAbsolute() ? QDir::cleanPath(absolutePath) : relativePath;
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
        parsed.type = parseLightType(light.value(QStringLiteral("type")).toString(QStringLiteral("point")));
        parsed.position = readVec3(light.value(QStringLiteral("position")), parsed.position);
        parsed.direction = readVec3(light.value(QStringLiteral("direction")), parsed.direction);
        parsed.color = readVec3(light.value(QStringLiteral("color")), parsed.color);
        parsed.ambientStrength = static_cast<float>(
            light.value(QStringLiteral("ambientStrength")).toDouble(parsed.ambientStrength));
        parsed.intensity =
            static_cast<float>(light.value(QStringLiteral("intensity")).toDouble(parsed.intensity));
        parsed.range = static_cast<float>(light.value(QStringLiteral("range")).toDouble(parsed.range));
        parsed.innerConeDegrees = static_cast<float>(
            light.value(QStringLiteral("innerConeDegrees")).toDouble(parsed.innerConeDegrees));
        parsed.outerConeDegrees = static_cast<float>(
            light.value(QStringLiteral("outerConeDegrees")).toDouble(parsed.outerConeDegrees));
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
        parsed.embeddedTextureBase64 = material.value(QStringLiteral("embeddedTextureBase64")).toString();
        parsed.tint = readVec3(material.value(QStringLiteral("tint")), parsed.tint);
        parsed.flipVertically = material.value(QStringLiteral("flipVertically")).toBool(parsed.flipVertically);
        config.materials.append(parsed);
    }

    const QJsonArray objects = root.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue& objectValue : objects) {
        const QJsonObject object = objectValue.toObject();
        RenderObjectConfig parsed;
        parsed.id = object.value(QStringLiteral("id")).toString();
        parsed.parentId = object.value(QStringLiteral("parentId")).toString();
        parsed.name = object.value(QStringLiteral("name")).toString(QStringLiteral("unnamed"));
        parsed.geometry =
            parseGeometryType(object.value(QStringLiteral("geometry")).toString(QStringLiteral("cube")));
        parsed.sourcePath = object.value(QStringLiteral("sourcePath")).toString();
        parsed.materialId = object.value(QStringLiteral("materialId")).toString();
        const QJsonArray materialIds = object.value(QStringLiteral("materialIds")).toArray();
        for (const QJsonValue& materialIdValue : materialIds) {
            parsed.materialIds.append(materialIdValue.toString());
        }
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
        config.debug.drawGrid =
            debug.value(QStringLiteral("drawGrid")).toBool(config.debug.drawGrid);
        config.debug.gridHalfExtent = static_cast<float>(
            debug.value(QStringLiteral("gridHalfExtent")).toDouble(config.debug.gridHalfExtent));
        config.debug.gridStep = static_cast<float>(
            debug.value(QStringLiteral("gridStep")).toDouble(config.debug.gridStep));
        config.debug.snapEnabled =
            debug.value(QStringLiteral("snapEnabled")).toBool(config.debug.snapEnabled);
        config.debug.rotateSnapDegrees = static_cast<float>(
            debug.value(QStringLiteral("rotateSnapDegrees")).toDouble(config.debug.rotateSnapDegrees));
        config.debug.scaleSnapStep = static_cast<float>(
            debug.value(QStringLiteral("scaleSnapStep")).toDouble(config.debug.scaleSnapStep));
    }

    return config;
}

void SceneConfig::saveToFile(const SceneConfig& config, const QString& path) {
    QJsonObject root;

    QJsonObject window;
    window.insert(QStringLiteral("title"), config.window.title);
    window.insert(QStringLiteral("width"), config.window.size.width());
    window.insert(QStringLiteral("height"), config.window.size.height());
    window.insert(QStringLiteral("clearColor"), writeVec4(config.window.clearColor));
    window.insert(QStringLiteral("captureMouse"), config.window.captureMouse);
    root.insert(QStringLiteral("window"), window);

    QJsonObject camera;
    camera.insert(QStringLiteral("position"), writeVec3(config.camera.position));
    camera.insert(QStringLiteral("yaw"), config.camera.yaw);
    camera.insert(QStringLiteral("pitch"), config.camera.pitch);
    camera.insert(QStringLiteral("fov"), config.camera.fov);
    camera.insert(QStringLiteral("nearClip"), config.camera.nearClip);
    camera.insert(QStringLiteral("farClip"), config.camera.farClip);
    camera.insert(QStringLiteral("moveSpeed"), config.camera.moveSpeed);
    camera.insert(QStringLiteral("lookSpeed"), config.camera.lookSpeed);
    root.insert(QStringLiteral("camera"), camera);

    QJsonArray lights;
    for (const LightConfig& light : config.lights) {
        QJsonObject lightObject;
        lightObject.insert(QStringLiteral("type"), lightTypeToString(light.type));
        lightObject.insert(QStringLiteral("position"), writeVec3(light.position));
        lightObject.insert(QStringLiteral("direction"), writeVec3(light.direction));
        lightObject.insert(QStringLiteral("color"), writeVec3(light.color));
        lightObject.insert(QStringLiteral("ambientStrength"), light.ambientStrength);
        lightObject.insert(QStringLiteral("intensity"), light.intensity);
        lightObject.insert(QStringLiteral("range"), light.range);
        lightObject.insert(QStringLiteral("innerConeDegrees"), light.innerConeDegrees);
        lightObject.insert(QStringLiteral("outerConeDegrees"), light.outerConeDegrees);
        lights.append(lightObject);
    }
    root.insert(QStringLiteral("lights"), lights);

    QJsonArray materials;
    for (const MaterialConfig& material : config.materials) {
        QJsonObject materialObject;
        materialObject.insert(QStringLiteral("id"), material.id);
        materialObject.insert(QStringLiteral("texturePath"), normalizeStoredPath(material.texturePath));
        if (!material.embeddedTextureBase64.isEmpty()) {
            materialObject.insert(QStringLiteral("embeddedTextureBase64"), material.embeddedTextureBase64);
        }
        materialObject.insert(QStringLiteral("tint"), writeVec3(material.tint));
        materialObject.insert(QStringLiteral("flipVertically"), material.flipVertically);
        materials.append(materialObject);
    }
    root.insert(QStringLiteral("materials"), materials);

    QJsonArray objects;
    for (const RenderObjectConfig& object : config.objects) {
        QJsonObject objectData;
        objectData.insert(QStringLiteral("id"), object.id);
        if (!object.parentId.isEmpty()) {
            objectData.insert(QStringLiteral("parentId"), object.parentId);
        }
        objectData.insert(QStringLiteral("name"), object.name);
        objectData.insert(QStringLiteral("geometry"), geometryTypeToString(object.geometry));
        if (!object.sourcePath.isEmpty()) {
            objectData.insert(QStringLiteral("sourcePath"), normalizeStoredPath(object.sourcePath));
        }
        objectData.insert(QStringLiteral("materialId"), object.materialId);
        if (!object.materialIds.isEmpty()) {
            QJsonArray materialIds;
            for (const QString& materialId : object.materialIds) {
                materialIds.append(materialId);
            }
            objectData.insert(QStringLiteral("materialIds"), materialIds);
        }
        objectData.insert(QStringLiteral("position"), writeVec3(object.position));
        objectData.insert(QStringLiteral("rotationDegrees"), writeVec3(object.rotationDegrees));
        objectData.insert(QStringLiteral("scale"), writeVec3(object.scale));
        objectData.insert(QStringLiteral("visible"), object.visible);
        objects.append(objectData);
    }
    root.insert(QStringLiteral("objects"), objects);

    QJsonObject debug;
    debug.insert(QStringLiteral("drawAxes"), config.debug.drawAxes);
    debug.insert(QStringLiteral("axesLength"), config.debug.axesLength);
    debug.insert(QStringLiteral("drawLightGizmo"), config.debug.drawLightGizmo);
    debug.insert(QStringLiteral("drawGrid"), config.debug.drawGrid);
    debug.insert(QStringLiteral("gridHalfExtent"), config.debug.gridHalfExtent);
    debug.insert(QStringLiteral("gridStep"), config.debug.gridStep);
    debug.insert(QStringLiteral("snapEnabled"), config.debug.snapEnabled);
    debug.insert(QStringLiteral("rotateSnapDegrees"), config.debug.rotateSnapDegrees);
    debug.insert(QStringLiteral("scaleSnapStep"), config.debug.scaleSnapStep);
    root.insert(QStringLiteral("debug"), debug);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error(("failed to open scene config for writing: " + path).toStdString());
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        throw std::runtime_error(("failed to write scene config: " + path).toStdString());
    }
    if (!file.commit()) {
        throw std::runtime_error(("failed to commit scene config: " + path).toStdString());
    }
}

}  // namespace renderer
