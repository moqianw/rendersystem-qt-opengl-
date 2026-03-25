#include "app/SceneDocument.hpp"

#include <QFileInfo>
#include <QUndoCommand>

#include <cmath>
#include <functional>

#include "app/SceneGraph.hpp"

namespace {

bool sameFloat(float left, float right) {
    return std::abs(left - right) <= 0.0001f;
}

bool sameVec3(const QVector3D& left, const QVector3D& right) {
    return sameFloat(left.x(), right.x()) && sameFloat(left.y(), right.y()) && sameFloat(left.z(), right.z());
}

bool sameVec4(const QVector4D& left, const QVector4D& right) {
    return sameFloat(left.x(), right.x()) && sameFloat(left.y(), right.y()) &&
        sameFloat(left.z(), right.z()) && sameFloat(left.w(), right.w());
}

bool sameWindow(const renderer::WindowConfig& left, const renderer::WindowConfig& right) {
    return left.size == right.size &&
        left.title == right.title &&
        sameVec4(left.clearColor, right.clearColor) &&
        left.captureMouse == right.captureMouse;
}

bool sameCamera(const renderer::CameraConfig& left, const renderer::CameraConfig& right) {
    return sameVec3(left.position, right.position) &&
        sameFloat(left.yaw, right.yaw) &&
        sameFloat(left.pitch, right.pitch) &&
        sameFloat(left.fov, right.fov) &&
        sameFloat(left.nearClip, right.nearClip) &&
        sameFloat(left.farClip, right.farClip) &&
        sameFloat(left.moveSpeed, right.moveSpeed) &&
        sameFloat(left.lookSpeed, right.lookSpeed);
}

bool sameLight(const renderer::LightConfig& left, const renderer::LightConfig& right) {
    return left.type == right.type &&
        sameVec3(left.position, right.position) &&
        sameVec3(left.direction, right.direction) &&
        sameVec3(left.color, right.color) &&
        sameFloat(left.ambientStrength, right.ambientStrength) &&
        sameFloat(left.intensity, right.intensity) &&
        sameFloat(left.range, right.range) &&
        sameFloat(left.innerConeDegrees, right.innerConeDegrees) &&
        sameFloat(left.outerConeDegrees, right.outerConeDegrees);
}

bool sameMaterial(const renderer::MaterialConfig& left, const renderer::MaterialConfig& right) {
    return left.id == right.id &&
        left.texturePath == right.texturePath &&
        left.embeddedTextureBase64 == right.embeddedTextureBase64 &&
        sameVec3(left.tint, right.tint) &&
        left.flipVertically == right.flipVertically;
}

bool sameObject(const renderer::RenderObjectConfig& left, const renderer::RenderObjectConfig& right) {
    return left.id == right.id &&
        left.parentId == right.parentId &&
        left.name == right.name &&
        left.geometry == right.geometry &&
        left.sourcePath == right.sourcePath &&
        left.materialId == right.materialId &&
        left.materialIds == right.materialIds &&
        sameVec3(left.position, right.position) &&
        sameVec3(left.rotationDegrees, right.rotationDegrees) &&
        sameVec3(left.scale, right.scale) &&
        left.visible == right.visible;
}

bool sameDebug(const renderer::DebugConfig& left, const renderer::DebugConfig& right) {
    return left.drawAxes == right.drawAxes &&
        sameFloat(left.axesLength, right.axesLength) &&
        left.drawLightGizmo == right.drawLightGizmo &&
        left.drawGrid == right.drawGrid &&
        sameFloat(left.gridHalfExtent, right.gridHalfExtent) &&
        sameFloat(left.gridStep, right.gridStep) &&
        left.snapEnabled == right.snapEnabled &&
        sameFloat(left.rotateSnapDegrees, right.rotateSnapDegrees) &&
        sameFloat(left.scaleSnapStep, right.scaleSnapStep);
}

bool sameScene(const renderer::SceneConfig& left, const renderer::SceneConfig& right) {
    if (!sameWindow(left.window, right.window) || !sameCamera(left.camera, right.camera) || !sameDebug(left.debug, right.debug)) {
        return false;
    }

    if (left.lights.size() != right.lights.size() ||
        left.materials.size() != right.materials.size() ||
        left.objects.size() != right.objects.size()) {
        return false;
    }

    for (int index = 0; index < left.lights.size(); ++index) {
        if (!sameLight(left.lights.at(index), right.lights.at(index))) {
            return false;
        }
    }

    for (int index = 0; index < left.materials.size(); ++index) {
        if (!sameMaterial(left.materials.at(index), right.materials.at(index))) {
            return false;
        }
    }

    for (int index = 0; index < left.objects.size(); ++index) {
        if (!sameObject(left.objects.at(index), right.objects.at(index))) {
            return false;
        }
    }

    return true;
}

QVector3D sanitizeLightDirection(const QVector3D& direction) {
    if (direction.lengthSquared() <= 1e-6f) {
        return QVector3D(-0.45f, -1.0f, -0.3f);
    }
    return direction.normalized();
}

renderer::LightConfig sanitizeLight(renderer::LightConfig light) {
    light.direction = sanitizeLightDirection(light.direction);
    light.range = qMax(0.1f, light.range);
    light.innerConeDegrees = qBound(0.1f, light.innerConeDegrees, 89.0f);
    light.outerConeDegrees = qBound(light.innerConeDegrees, light.outerConeDegrees, 89.5f);
    return light;
}

class LambdaUndoCommand final : public QUndoCommand {
public:
    using Callback = std::function<void()>;

    LambdaUndoCommand(const QString& text, Callback undo, Callback redo)
        : QUndoCommand(text),
          undo_(std::move(undo)),
          redo_(std::move(redo)) {
    }

    void undo() override {
        undo_();
    }

    void redo() override {
        if (firstRedo_) {
            firstRedo_ = false;
            return;
        }

        redo_();
    }

private:
    Callback undo_;
    Callback redo_;
    bool firstRedo_ = true;
};

}  // namespace

namespace renderer {

SceneDocument::SceneDocument(SceneConfig* scene, const QString& scenePath, QObject* parent)
    : QObject(parent),
      scene_(scene),
      scenePath_(scenePath),
      undoStack_(this) {
    if (scene_) {
        scenegraph::ensureObjectIds(scene_);
        for (LightConfig& light : scene_->lights) {
            light = sanitizeLight(light);
        }
    }

    connect(&undoStack_, &QUndoStack::indexChanged, this, [this](int) {
        updateDirtyState(!undoStack_.isClean() || sceneEditSessionActive_);
    });
}

void SceneDocument::setApplySceneCallback(ApplySceneCallback callback) {
    applySceneCallback_ = std::move(callback);
}

QUndoStack* SceneDocument::undoStack() {
    return &undoStack_;
}

const QString& SceneDocument::scenePath() const {
    return scenePath_;
}

bool SceneDocument::isDirty() const {
    return dirty_;
}

bool SceneDocument::hasUnsavedChanges() const {
    return dirty_ || sceneEditSessionActive_;
}

QString SceneDocument::windowCaption() const {
    const QString dirtyMarker = dirty_ ? QStringLiteral(" *") : QString();
    const QString fileName = QFileInfo(scenePath_).fileName();
    const QString title = scene_ ? scene_->window.title : QStringLiteral("Scene");
    return QStringLiteral("%1%2 - %3").arg(title, dirtyMarker, fileName);
}

void SceneDocument::clearHistory() {
    undoStack_.clear();
    undoStack_.setClean();
    updateDirtyState(sceneEditSessionActive_);
}

void SceneDocument::markDirty() {
    updateDirtyState(true);
}

void SceneDocument::setClean() {
    undoStack_.setClean();
    updateDirtyState(false);
}

void SceneDocument::beginEditSession(const QString& description, SceneUpdateMode updateMode, int selectionToken) {
    if (!scene_) {
        return;
    }

    if (!sceneEditSessionActive_) {
        sceneEditSessionActive_ = true;
        sceneEditBefore_ = *scene_;
        sceneEditBeforeSelection_ = selectionToken;
        sceneEditDescription_ = description;
        sceneEditUpdateMode_ = updateMode;
        updateDirtyState(true);
        return;
    }

    sceneEditUpdateMode_ =
        sceneEditUpdateMode_ == SceneUpdateMode::ReloadResources || updateMode == SceneUpdateMode::ReloadResources
        ? SceneUpdateMode::ReloadResources
        : SceneUpdateMode::TransformsOnly;
}

void SceneDocument::commitEditSession(int selectionToken) {
    if (!sceneEditSessionActive_ || !scene_) {
        return;
    }

    const SceneConfig before = sceneEditBefore_;
    const int beforeSelection = sceneEditBeforeSelection_;
    const QString description = sceneEditDescription_;
    const SceneUpdateMode updateMode = sceneEditUpdateMode_;

    sceneEditSessionActive_ = false;
    sceneEditDescription_.clear();
    sceneEditUpdateMode_ = SceneUpdateMode::TransformsOnly;

    if (sameScene(before, *scene_)) {
        updateDirtyState(!undoStack_.isClean());
        return;
    }

    pushSceneCommand(before, *scene_, beforeSelection, selectionToken, description, updateMode);
}

void SceneDocument::pushSceneCommand(
    const SceneConfig& before,
    const SceneConfig& after,
    int beforeSelection,
    int afterSelection,
    const QString& description,
    SceneUpdateMode updateMode) {
    if (sameScene(before, after)) {
        return;
    }

    undoStack_.push(new LambdaUndoCommand(
        description,
        [this, before, beforeSelection, updateMode]() {
            applySceneState(before, beforeSelection, updateMode);
        },
        [this, after, afterSelection, updateMode]() {
            applySceneState(after, afterSelection, updateMode);
        }));
}

void SceneDocument::applySceneState(
    const SceneConfig& scene,
    int selectionToken,
    SceneUpdateMode updateMode,
    bool resetCamera) {
    if (!scene_) {
        return;
    }

    *scene_ = scene;
    scenegraph::ensureObjectIds(scene_);
    for (LightConfig& light : scene_->lights) {
        light = sanitizeLight(light);
    }

    if (applySceneCallback_) {
        applySceneCallback_(*scene_, selectionToken, updateMode, resetCamera);
    }

    updateDirtyState(!undoStack_.isClean() || sceneEditSessionActive_);
}

void SceneDocument::save() {
    if (!scene_) {
        return;
    }

    scenegraph::ensureObjectIds(scene_);
    SceneConfig::saveToFile(*scene_, scenePath_);
    undoStack_.setClean();
    updateDirtyState(false);
}

void SceneDocument::reload(int selectionToken) {
    if (!scene_) {
        return;
    }

    SceneConfig loaded = SceneConfig::loadFromFile(scenePath_);
    scenegraph::ensureObjectIds(&loaded);
    for (LightConfig& light : loaded.lights) {
        light = sanitizeLight(light);
    }

    *scene_ = loaded;
    if (applySceneCallback_) {
        applySceneCallback_(*scene_, selectionToken, SceneUpdateMode::ReloadResources, true);
    }

    sceneEditSessionActive_ = false;
    sceneEditDescription_.clear();
    sceneEditUpdateMode_ = SceneUpdateMode::TransformsOnly;
    undoStack_.clear();
    undoStack_.setClean();
    updateDirtyState(false);
}

void SceneDocument::updateDirtyState(bool dirty) {
    if (dirty_ == dirty) {
        return;
    }

    dirty_ = dirty;
    emit dirtyChanged(dirty_);
}

}  // namespace renderer
