#include "app/MainWindow.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>

#include "app/ModelLoader.hpp"
#include "app/SceneDocument.hpp"
#include "app/SceneGraph.hpp"

namespace {

const QVector3D kDuplicateOffset(1.5f, 0.0f, 1.5f);
constexpr int kSceneTreeItemTypeRole = Qt::UserRole;
constexpr int kSceneTreeItemIndexRole = Qt::UserRole + 1;

enum class SceneTreeItemType {
    Object = 1,
    Light = 2
};

QString geometryLabel(renderer::GeometryType geometry) {
    switch (geometry) {
    case renderer::GeometryType::Cube:
        return QStringLiteral("Cube");
    case renderer::GeometryType::Model:
        return QStringLiteral("Model");
    }

    return QStringLiteral("Object");
}

QString lightTypeLabel(renderer::LightType type) {
    switch (type) {
    case renderer::LightType::Directional:
        return QStringLiteral("Directional");
    case renderer::LightType::Point:
        return QStringLiteral("Point");
    case renderer::LightType::Spot:
        return QStringLiteral("Spot");
    }

    return QStringLiteral("Light");
}

QString objectDisplayName(const renderer::RenderObjectConfig& object, int index) {
    const QString baseName = object.name.isEmpty()
        ? QStringLiteral("object_%1").arg(index + 1)
        : object.name;
    return QStringLiteral("%1. %2 [%3]").arg(index + 1).arg(baseName, geometryLabel(object.geometry));
}

QString lightDisplayName(const renderer::LightConfig& light, int index) {
    return QStringLiteral("%1. %2 Light").arg(index + 1).arg(lightTypeLabel(light.type));
}

int lightSelectionToken(int lightIndex) {
    return -2 - lightIndex;
}

int lightIndexFromSelectionToken(int selectionToken) {
    return selectionToken <= -2 ? (-selectionToken) - 2 : -1;
}

QString uniqueMaterialId(const renderer::SceneConfig& scene, const QString& baseName) {
    QString candidate = baseName.trimmed();
    if (candidate.isEmpty()) {
        candidate = QStringLiteral("material");
    }

    QSet<QString> ids;
    for (const renderer::MaterialConfig& material : scene.materials) {
        ids.insert(material.id);
    }

    if (!ids.contains(candidate)) {
        return candidate;
    }

    int suffix = 2;
    while (ids.contains(QStringLiteral("%1_%2").arg(candidate).arg(suffix))) {
        ++suffix;
    }

    return QStringLiteral("%1_%2").arg(candidate).arg(suffix);
}

QString formatVector(const QVector3D& value) {
    return QStringLiteral("[%1, %2, %3]")
        .arg(value.x(), 0, 'f', 2)
        .arg(value.y(), 0, 'f', 2)
        .arg(value.z(), 0, 'f', 2);
}

QString transformModeLabel(renderer::RenderWidget::TransformMode mode) {
    switch (mode) {
    case renderer::RenderWidget::TransformMode::Translate:
        return QStringLiteral("Move");
    case renderer::RenderWidget::TransformMode::Rotate:
        return QStringLiteral("Rotate");
    case renderer::RenderWidget::TransformMode::Scale:
        return QStringLiteral("Scale");
    }

    return QStringLiteral("Transform");
}

void setVectorEditors(QDoubleSpinBox* editors[3], const QVector3D& value) {
    editors[0]->setValue(value.x());
    editors[1]->setValue(value.y());
    editors[2]->setValue(value.z());
}

QVector3D readVectorEditors(QDoubleSpinBox* editors[3]) {
    return QVector3D(
        static_cast<float>(editors[0]->value()),
        static_cast<float>(editors[1]->value()),
        static_cast<float>(editors[2]->value()));
}

bool sameFloat(float left, float right) {
    return std::abs(left - right) <= 0.0001f;
}

bool sameVec3(const QVector3D& left, const QVector3D& right) {
    return sameFloat(left.x(), right.x()) && sameFloat(left.y(), right.y()) && sameFloat(left.z(), right.z());
}

bool sameVec4(const QVector4D& left, const QVector4D& right) {
    return sameFloat(left.x(), right.x()) && sameFloat(left.y(), right.y()) && sameFloat(left.z(), right.z()) && sameFloat(left.w(), right.w());
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

QVector<int> normalizeSelectionIndices(const renderer::SceneConfig& scene, const QVector<int>& indices) {
    QVector<int> normalized;
    QSet<int> visited;
    for (int index : indices) {
        if (index < 0 || index >= scene.objects.size() || visited.contains(index)) {
            continue;
        }
        visited.insert(index);
        normalized.append(index);
    }
    std::sort(normalized.begin(), normalized.end());
    return normalized;
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

QString ensureImportedMaterial(
    renderer::SceneConfig* scene,
    const renderer::ImportedMaterialData& importedMaterial,
    const QString& baseName,
    int materialIndex) {
    if (!scene) {
        return {};
    }

    const QString texturePath = normalizeStoredPath(importedMaterial.texturePath);
    const QString embeddedTextureBase64 = importedMaterial.embeddedTextureData.isEmpty()
        ? QString()
        : QString::fromLatin1(importedMaterial.embeddedTextureData.toBase64());
    const bool usesCustomMaterial =
        !texturePath.isEmpty() ||
        !embeddedTextureBase64.isEmpty() ||
        !sameVec3(importedMaterial.tint, QVector3D(1.0f, 1.0f, 1.0f));
    if (!usesCustomMaterial) {
        return {};
    }

    for (const renderer::MaterialConfig& existing : scene->materials) {
        if (existing.texturePath == texturePath &&
            existing.embeddedTextureBase64 == embeddedTextureBase64 &&
            sameVec3(existing.tint, importedMaterial.tint) &&
            existing.flipVertically == importedMaterial.flipVertically) {
            return existing.id;
        }
    }

    renderer::MaterialConfig material;
    material.id = uniqueMaterialId(
        *scene,
        materialIndex == 0
            ? QStringLiteral("%1_material").arg(baseName)
            : QStringLiteral("%1_material_%2").arg(baseName).arg(materialIndex + 1));
    material.texturePath = texturePath;
    material.embeddedTextureBase64 = embeddedTextureBase64;
    material.tint = importedMaterial.tint;
    material.flipVertically = importedMaterial.flipVertically;
    scene->materials.append(material);
    return material.id;
}

}  // namespace

namespace renderer {

MainWindow::MainWindow(const SceneConfig& scene, const QString& scenePath, QWidget* parent)
    : QMainWindow(parent),
      scene_(scene),
      document_(new SceneDocument(&scene_, scenePath, this)) {
    resize(scene_.window.size.expandedTo(QSize(1280, 820)));
    setMinimumSize(1024, 720);
    setDockNestingEnabled(true);
    setDockOptions(
        QMainWindow::AnimatedDocks |
        QMainWindow::AllowNestedDocks |
        QMainWindow::AllowTabbedDocks);

    createSceneResourceActions();
    createDocks();
    createMenus();

    document_->setApplySceneCallback([this](
                                         const SceneConfig& sceneState,
                                         int selectionToken,
                                         RenderWidget::SceneUpdateMode updateMode,
                                         bool resetCamera) {
        applySceneState(sceneState, selectionToken, updateMode, resetCamera);
    });
    connect(document_, &SceneDocument::dirtyChanged, this, [this](bool) {
        updateWindowCaption();
    });

    connect(renderWidget_, &RenderWidget::selectionChanged, this, [this](const QVector<int>& indices, int activeIndex) {
        setSelectionState(indices, activeIndex);
    });
    connect(renderWidget_, &RenderWidget::lightSelectionChanged, this, [this](int index) {
        setLightSelectionState(index);
    });
    connect(
        renderWidget_,
        &RenderWidget::objectTransformInteractionStarted,
        this,
        [this](int index, RenderWidget::TransformMode mode) {
            if (index < 0 || index >= scene_.objects.size()) {
                return;
            }
            beginSceneEditSession(
                QStringLiteral("%1 %2").arg(transformModeLabel(mode), scene_.objects.at(index).name),
                RenderWidget::SceneUpdateMode::TransformsOnly);
        });
    connect(
        renderWidget_,
        &RenderWidget::objectTransformPreview,
        this,
        [this](int index, const QVector3D& position, const QVector3D& rotationDegrees, const QVector3D& scale) {
            applyViewportTransformPreview(index, position, rotationDegrees, scale);
        });
    connect(
        renderWidget_,
        &RenderWidget::objectTransformInteractionFinished,
        this,
        [this](
            int index,
            const QVector3D& position,
            const QVector3D& rotationDegrees,
            const QVector3D& scale,
            RenderWidget::TransformMode mode) {
            commitViewportTransform(index, position, rotationDegrees, scale, mode);
        });
    connect(
        renderWidget_,
        &RenderWidget::lightTransformInteractionStarted,
        this,
        [this](int index, RenderWidget::TransformMode mode) {
            if (index < 0 || index >= scene_.lights.size()) {
                return;
            }
            beginSceneEditSession(
                QStringLiteral("%1 %2").arg(transformModeLabel(mode), lightDisplayName(scene_.lights.at(index), index)),
                RenderWidget::SceneUpdateMode::TransformsOnly);
        });
    connect(
        renderWidget_,
        &RenderWidget::lightTransformPreview,
        this,
        [this](int index, const QVector3D& position, const QVector3D& direction) {
            applyViewportLightTransformPreview(index, position, direction);
        });
    connect(
        renderWidget_,
        &RenderWidget::lightTransformInteractionFinished,
        this,
        [this](int index, const QVector3D& position, const QVector3D& direction, RenderWidget::TransformMode mode) {
            commitViewportLightTransform(index, position, direction, mode);
        });
    connect(
        renderWidget_,
        &RenderWidget::cameraStateChanged,
        this,
        [this](const QVector3D& position, const QVector3D& target, float distance) {
            updateCameraPanel(position, target, distance);
        });
    connect(renderWidget_, &RenderWidget::transformModeChanged, this, [this](RenderWidget::TransformMode mode) {
        setTransformMode(mode);
    });
    connect(renderWidget_, &RenderWidget::coordinateSpaceChanged, this, [this](RenderWidget::CoordinateSpace space) {
        setCoordinateSpace(space);
    });

    refreshMaterialList();
    refreshLightsPanel();
    refreshToolsPanel();
    refreshObjectTree();

    if (!restoreEditorLayout()) {
        restoreDefaultLayout();
    }

    renderWidget_->resetCamera();
    if (!scene_.objects.isEmpty()) {
        setSelectionState({0}, 0);
    } else if (!scene_.lights.isEmpty()) {
        setLightSelectionState(0);
    } else {
        updateCameraPanel(scene_.camera.position, scene_.camera.position + QVector3D(0.0f, 0.0f, -10.0f), 10.0f);
    }

    document_->clearHistory();
    updateWindowCaption();
    statusBar()->showMessage(
        QStringLiteral("Scene loaded from %1").arg(QFileInfo(document_->scenePath()).fileName()),
        3000);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    commitInspectorTransformEdits();
    commitLightEdits();
    if (!maybeSaveChanges(QStringLiteral("close the editor"))) {
        event->ignore();
        return;
    }

    saveEditorLayout();
    event->accept();
}

void MainWindow::refreshObjectTree() {
    scenegraph::ensureObjectIds(&scene_);

    QSignalBlocker blocker(objectTree_);
    objectTree_->clear();

    std::function<void(QTreeWidgetItem*, int)> appendItem = [&](QTreeWidgetItem* parentItem, int objectIndex) {
        const RenderObjectConfig& object = scene_.objects.at(objectIndex);
        auto* item = new QTreeWidgetItem;
        item->setText(0, objectDisplayName(object, objectIndex));
        item->setData(0, kSceneTreeItemTypeRole, static_cast<int>(SceneTreeItemType::Object));
        item->setData(0, kSceneTreeItemIndexRole, objectIndex);
        item->setToolTip(
            0,
            QStringLiteral("id: %1\nparent: %2\nsource: %3")
                .arg(object.id, object.parentId.isEmpty() ? QStringLiteral("<root>") : object.parentId, object.sourcePath));

        if (parentItem) {
            parentItem->addChild(item);
        } else {
            objectTree_->addTopLevelItem(item);
        }

        for (int childIndex : scenegraph::childIndices(scene_, object.id)) {
            appendItem(item, childIndex);
        }
    };

    for (int rootIndex : scenegraph::rootIndices(scene_)) {
        appendItem(nullptr, rootIndex);
    }

    if (!scene_.lights.isEmpty()) {
        auto* lightsRoot = new QTreeWidgetItem;
        lightsRoot->setText(0, QStringLiteral("Lights"));
        lightsRoot->setFlags(lightsRoot->flags() & ~Qt::ItemIsSelectable);
        objectTree_->addTopLevelItem(lightsRoot);

        for (int lightIndex = 0; lightIndex < scene_.lights.size(); ++lightIndex) {
            auto* lightItem = new QTreeWidgetItem(lightsRoot);
            lightItem->setText(0, lightDisplayName(scene_.lights.at(lightIndex), lightIndex));
            lightItem->setData(0, kSceneTreeItemTypeRole, static_cast<int>(SceneTreeItemType::Light));
            lightItem->setData(0, kSceneTreeItemIndexRole, lightIndex);
            lightItem->setToolTip(
                0,
                QStringLiteral("type: %1\nposition: %2")
                    .arg(lightTypeLabel(scene_.lights.at(lightIndex).type), formatVector(scene_.lights.at(lightIndex).position)));
        }
    }

    objectTree_->expandAll();
    if (currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size()) {
        setLightSelectionState(currentLightIndex_);
    } else {
        setSelectionState(selectedObjectIndices_, currentObjectIndex_);
    }
}

void MainWindow::refreshMaterialList() {
    {
        QSignalBlocker blocker(materialList_);
        materialList_->clear();
        for (const MaterialConfig& material : scene_.materials) {
            const QString textureLabel = !material.texturePath.isEmpty()
                ? material.texturePath
                : !material.embeddedTextureBase64.isEmpty()
                    ? QStringLiteral("<embedded texture>")
                    : QStringLiteral("<no texture>");
            const QString label = QStringLiteral("%1  ->  %2%3")
                .arg(material.id.isEmpty() ? QStringLiteral("__default__") : material.id)
                .arg(textureLabel)
                .arg(material.flipVertically ? QStringLiteral(" [flipY]") : QString());
            materialList_->addItem(label);
        }
    }

    {
        QSignalBlocker blocker(materialCombo_);
        materialCombo_->clear();
        for (const MaterialConfig& material : scene_.materials) {
            materialCombo_->addItem(material.id.isEmpty() ? QStringLiteral("__default__") : material.id);
        }
    }
}

void MainWindow::refreshLightsPanel() {
    if (currentLightIndex_ >= scene_.lights.size()) {
        currentLightIndex_ = scene_.lights.isEmpty() ? -1 : (scene_.lights.size() - 1);
    }
    refreshLightInspector();
}

void MainWindow::refreshLightInspector() {
    if (!lightTypeCombo_) {
        return;
    }

    const bool hasLight = currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size();
    syncingLights_ = true;

    lightTypeCombo_->setEnabled(hasLight);

    for (int index = 0; index < 3; ++index) {
        lightPositionEdits_[index]->setEnabled(hasLight);
        lightDirectionEdits_[index]->setEnabled(hasLight);
        lightColorEdits_[index]->setEnabled(hasLight);
    }
    lightAmbientEdit_->setEnabled(hasLight);
    lightIntensityEdit_->setEnabled(hasLight);
    lightRangeEdit_->setEnabled(hasLight);
    lightInnerConeEdit_->setEnabled(hasLight);
    lightOuterConeEdit_->setEnabled(hasLight);

    if (!hasLight) {
        lightTypeCombo_->setCurrentIndex(0);
        setVectorEditors(lightPositionEdits_, QVector3D());
        setVectorEditors(lightDirectionEdits_, QVector3D(-0.45f, -1.0f, -0.3f));
        setVectorEditors(lightColorEdits_, QVector3D(1.0f, 1.0f, 1.0f));
        lightAmbientEdit_->setValue(0.18);
        lightIntensityEdit_->setValue(1.15);
        lightRangeEdit_->setValue(24.0);
        lightInnerConeEdit_->setValue(18.0);
        lightOuterConeEdit_->setValue(28.0);
        syncingLights_ = false;
        return;
    }

    const LightConfig light = sanitizeLight(scene_.lights.at(currentLightIndex_));
    const int lightTypeIndex = lightTypeCombo_->findData(static_cast<int>(light.type));
    lightTypeCombo_->setCurrentIndex(lightTypeIndex < 0 ? 0 : lightTypeIndex);
    setVectorEditors(lightPositionEdits_, light.position);
    setVectorEditors(lightDirectionEdits_, light.direction);
    setVectorEditors(lightColorEdits_, light.color);
    lightAmbientEdit_->setValue(light.ambientStrength);
    lightIntensityEdit_->setValue(light.intensity);
    lightRangeEdit_->setValue(light.range);
    lightInnerConeEdit_->setValue(light.innerConeDegrees);
    lightOuterConeEdit_->setValue(light.outerConeDegrees);

    const bool usesDirection = light.type != LightType::Point;
    const bool usesRange = light.type != LightType::Directional;
    const bool usesCone = light.type == LightType::Spot;
    for (QDoubleSpinBox* editor : lightDirectionEdits_) {
        editor->setEnabled(hasLight && usesDirection);
    }
    lightRangeEdit_->setEnabled(hasLight && usesRange);
    lightInnerConeEdit_->setEnabled(hasLight && usesCone);
    lightOuterConeEdit_->setEnabled(hasLight && usesCone);

    syncingLights_ = false;
}

void MainWindow::refreshToolsPanel() {
    applyingSceneState_ = true;
    if (transformModeCombo_) {
        QSignalBlocker blocker(transformModeCombo_);
        const int index = transformModeCombo_->findData(static_cast<int>(renderWidget_->transformMode()));
        transformModeCombo_->setCurrentIndex(index < 0 ? 0 : index);
    }
    if (coordinateSpaceCombo_) {
        QSignalBlocker blocker(coordinateSpaceCombo_);
        const int index = coordinateSpaceCombo_->findData(static_cast<int>(renderWidget_->coordinateSpace()));
        coordinateSpaceCombo_->setCurrentIndex(index < 0 ? 0 : index);
    }
    if (snapEnabledCheck_) {
        QSignalBlocker blocker(snapEnabledCheck_);
        snapEnabledCheck_->setChecked(scene_.debug.snapEnabled);
    }
    if (moveSnapStepEdit_) {
        QSignalBlocker blocker(moveSnapStepEdit_);
        moveSnapStepEdit_->setValue(scene_.debug.gridStep);
    }
    if (rotateSnapStepEdit_) {
        QSignalBlocker blocker(rotateSnapStepEdit_);
        rotateSnapStepEdit_->setValue(scene_.debug.rotateSnapDegrees);
    }
    if (scaleSnapStepEdit_) {
        QSignalBlocker blocker(scaleSnapStepEdit_);
        scaleSnapStepEdit_->setValue(scene_.debug.scaleSnapStep);
    }
    renderWidget_->setSnapEnabled(scene_.debug.snapEnabled);
    renderWidget_->setMoveSnapStep(scene_.debug.gridStep);
    renderWidget_->setRotateSnapStep(scene_.debug.rotateSnapDegrees);
    renderWidget_->setScaleSnapStep(scene_.debug.scaleSnapStep);
    applyingSceneState_ = false;
}

void MainWindow::setSelectionState(const QVector<int>& indices, int currentIndex) {
    QVector<int> normalized = normalizeSelectionIndices(scene_, indices);
    int nextCurrent = currentIndex;

    if (nextCurrent >= 0 && nextCurrent < scene_.objects.size() && !normalized.contains(nextCurrent)) {
        normalized.append(nextCurrent);
        std::sort(normalized.begin(), normalized.end());
    }

    if (normalized.isEmpty()) {
        nextCurrent = -1;
    } else if (nextCurrent < 0 || nextCurrent >= scene_.objects.size()) {
        nextCurrent = normalized.constLast();
    }

    selectedObjectIndices_ = normalized;
    currentObjectIndex_ = nextCurrent;
    currentLightIndex_ = -1;

    if (objectTree_) {
        QSignalBlocker blocker(objectTree_);
        objectTree_->clearSelection();
        QTreeWidgetItem* currentItem = nullptr;
        for (QTreeWidgetItemIterator it(objectTree_); *it; ++it) {
            auto* item = *it;
            if (item->data(0, kSceneTreeItemTypeRole).toInt() != static_cast<int>(SceneTreeItemType::Object)) {
                continue;
            }
            const int itemIndex = item->data(0, kSceneTreeItemIndexRole).toInt();
            if (selectedObjectIndices_.contains(itemIndex)) {
                item->setSelected(true);
            }
            if (itemIndex == currentObjectIndex_) {
                currentItem = item;
            }
        }
        objectTree_->setCurrentItem(currentItem);
    }

    renderWidget_->setSelection(selectedObjectIndices_, currentObjectIndex_);
    refreshInspector();
    refreshLightInspector();

    if (selectedObjectIndices_.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Selection cleared"), 1500);
    } else if (selectedObjectIndices_.size() == 1 && currentObjectIndex_ >= 0 && currentObjectIndex_ < scene_.objects.size()) {
        statusBar()->showMessage(
            QStringLiteral("Selected %1").arg(scene_.objects.at(currentObjectIndex_).name),
            1500);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Selected %1 objects").arg(selectedObjectIndices_.size()),
            1500);
    }
}

void MainWindow::setLightSelectionState(int currentIndex) {
    currentLightIndex_ =
        currentIndex >= 0 && currentIndex < scene_.lights.size() ? currentIndex : -1;
    selectedObjectIndices_.clear();
    currentObjectIndex_ = -1;

    if (objectTree_) {
        QSignalBlocker blocker(objectTree_);
        objectTree_->clearSelection();
        QTreeWidgetItem* currentItem = nullptr;
        for (QTreeWidgetItemIterator it(objectTree_); *it; ++it) {
            auto* item = *it;
            if (item->data(0, kSceneTreeItemTypeRole).toInt() != static_cast<int>(SceneTreeItemType::Light)) {
                continue;
            }

            const int itemIndex = item->data(0, kSceneTreeItemIndexRole).toInt();
            if (itemIndex == currentLightIndex_) {
                item->setSelected(true);
                currentItem = item;
            }
        }
        objectTree_->setCurrentItem(currentItem);
    }

    renderWidget_->setSelectedLightIndex(currentLightIndex_);
    refreshInspector();
    refreshLightInspector();

    if (currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size()) {
        statusBar()->showMessage(
            QStringLiteral("Selected %1").arg(lightDisplayName(scene_.lights.at(currentLightIndex_), currentLightIndex_)),
            1500);
    } else {
        statusBar()->showMessage(QStringLiteral("Selection cleared"), 1500);
    }
}

int MainWindow::currentSelectionToken() const {
    if (currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size()) {
        return lightSelectionToken(currentLightIndex_);
    }
    if (currentObjectIndex_ >= 0 && currentObjectIndex_ < scene_.objects.size()) {
        return currentObjectIndex_;
    }
    return -1;
}

void MainWindow::handleObjectTreeSelectionChanged() {
    QVector<int> indices;
    QVector<int> lightIndices;
    for (QTreeWidgetItem* item : objectTree_->selectedItems()) {
        const SceneTreeItemType itemType =
            static_cast<SceneTreeItemType>(item->data(0, kSceneTreeItemTypeRole).toInt());
        const int itemIndex = item->data(0, kSceneTreeItemIndexRole).toInt();
        if (itemType == SceneTreeItemType::Object) {
            indices.append(itemIndex);
        } else if (itemType == SceneTreeItemType::Light) {
            lightIndices.append(itemIndex);
        }
    }

    int currentIndex = -1;
    int currentLightIndex = -1;
    if (QTreeWidgetItem* currentItem = objectTree_->currentItem()) {
        const SceneTreeItemType currentType =
            static_cast<SceneTreeItemType>(currentItem->data(0, kSceneTreeItemTypeRole).toInt());
        const int itemIndex = currentItem->data(0, kSceneTreeItemIndexRole).toInt();
        if (currentType == SceneTreeItemType::Object) {
            currentIndex = itemIndex;
        } else if (currentType == SceneTreeItemType::Light) {
            currentLightIndex = itemIndex;
        }
    }

    if (!lightIndices.isEmpty()) {
        if (currentLightIndex < 0 || !lightIndices.contains(currentLightIndex)) {
            currentLightIndex = lightIndices.constLast();
        }
        setLightSelectionState(currentLightIndex);
        return;
    }

    setSelectionState(indices, currentIndex);
}

void MainWindow::refreshInspector() {
    const bool hasSelection =
        currentObjectIndex_ >= 0 && currentObjectIndex_ < scene_.objects.size();

    syncingInspector_ = true;

    nameEdit_->setEnabled(hasSelection);
    materialCombo_->setEnabled(hasSelection);
    visibleCheck_->setEnabled(hasSelection);
    for (int index = 0; index < 3; ++index) {
        positionEdits_[index]->setEnabled(hasSelection);
        rotationEdits_[index]->setEnabled(hasSelection);
        scaleEdits_[index]->setEnabled(hasSelection);
    }

    if (!hasSelection) {
        nameEdit_->clear();
        materialCombo_->setCurrentText(QString());
        visibleCheck_->setChecked(false);
        setVectorEditors(positionEdits_, QVector3D());
        setVectorEditors(rotationEdits_, QVector3D());
        setVectorEditors(scaleEdits_, QVector3D(1.0f, 1.0f, 1.0f));
        syncingInspector_ = false;
        return;
    }

    const RenderObjectConfig& object = scene_.objects.at(currentObjectIndex_);
    nameEdit_->setText(object.name);
    nameEdit_->setPlaceholderText(
        selectedObjectIndices_.size() > 1
            ? QStringLiteral("Editing active object (%1 selected)").arg(selectedObjectIndices_.size())
            : QString());
    if (materialCombo_->findText(object.materialId) == -1 && !object.materialId.isEmpty()) {
        materialCombo_->addItem(object.materialId);
    }
    materialCombo_->setCurrentText(object.materialId);
    visibleCheck_->setChecked(object.visible);
    setVectorEditors(positionEdits_, object.position);
    setVectorEditors(rotationEdits_, object.rotationDegrees);
    setVectorEditors(scaleEdits_, object.scale);

    syncingInspector_ = false;
}

void MainWindow::previewLightEdits() {
    if (syncingLights_ || applyingSceneState_) {
        return;
    }
    if (currentLightIndex_ < 0 || currentLightIndex_ >= scene_.lights.size()) {
        return;
    }

    beginSceneEditSession(QStringLiteral("Edit Light"), RenderWidget::SceneUpdateMode::TransformsOnly);

    LightConfig light = scene_.lights.at(currentLightIndex_);
    light.type = static_cast<LightType>(lightTypeCombo_->currentData().toInt());
    light.position = readVectorEditors(lightPositionEdits_);
    light.direction = readVectorEditors(lightDirectionEdits_);
    light.color = readVectorEditors(lightColorEdits_);
    light.ambientStrength = static_cast<float>(lightAmbientEdit_->value());
    light.intensity = static_cast<float>(lightIntensityEdit_->value());
    light.range = static_cast<float>(lightRangeEdit_->value());
    light.innerConeDegrees = static_cast<float>(lightInnerConeEdit_->value());
    light.outerConeDegrees = static_cast<float>(lightOuterConeEdit_->value());
    scene_.lights[currentLightIndex_] = sanitizeLight(light);

    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::TransformsOnly);
    renderWidget_->setSelectedLightIndex(currentLightIndex_);
    if (objectTree_) {
        QTreeWidgetItem* currentItem = objectTree_->currentItem();
        if (currentItem &&
            currentItem->data(0, kSceneTreeItemTypeRole).toInt() == static_cast<int>(SceneTreeItemType::Light) &&
            currentItem->data(0, kSceneTreeItemIndexRole).toInt() == currentLightIndex_) {
            currentItem->setText(0, lightDisplayName(scene_.lights.at(currentLightIndex_), currentLightIndex_));
        }
    }
    markSceneDirty(QStringLiteral("Updated %1").arg(lightDisplayName(scene_.lights.at(currentLightIndex_), currentLightIndex_)));
}

void MainWindow::commitLightEdits() {
    if (syncingLights_ || applyingSceneState_) {
        return;
    }

    commitSceneEditSession();
    refreshLightInspector();
}

void MainWindow::previewInspectorTransformEdits() {
    if (syncingInspector_ || applyingSceneState_) {
        return;
    }
    if (currentObjectIndex_ < 0 || currentObjectIndex_ >= scene_.objects.size()) {
        return;
    }

    beginSceneEditSession(QStringLiteral("Edit Transform"), RenderWidget::SceneUpdateMode::TransformsOnly);

    RenderObjectConfig& object = scene_.objects[currentObjectIndex_];
    object.position = readVectorEditors(positionEdits_);
    object.rotationDegrees = readVectorEditors(rotationEdits_);
    object.scale = readVectorEditors(scaleEdits_);

    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::TransformsOnly);
    renderWidget_->setSelection(selectedObjectIndices_, currentObjectIndex_);
    markSceneDirty(QStringLiteral("Updated %1 transform").arg(object.name));
}

void MainWindow::commitInspectorTransformEdits() {
    commitSceneEditSession();
}

void MainWindow::applyInspectorMetadataEdits() {
    if (syncingInspector_ || applyingSceneState_) {
        return;
    }
    if (currentObjectIndex_ < 0 || currentObjectIndex_ >= scene_.objects.size()) {
        return;
    }

    commitInspectorTransformEdits();
    commitLightEdits();

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    RenderObjectConfig& object = scene_.objects[currentObjectIndex_];

    QString objectName = nameEdit_->text().trimmed();
    if (objectName.isEmpty()) {
        objectName = QStringLiteral("object_%1").arg(currentObjectIndex_ + 1);
        QSignalBlocker blocker(nameEdit_);
        nameEdit_->setText(objectName);
    }

    object.name = objectName;
    object.materialId = materialCombo_->currentText().trimmed();
    object.visible = visibleCheck_->isChecked();

    refreshObjectTree();
    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::TransformsOnly);
    renderWidget_->setSelection(selectedObjectIndices_, currentObjectIndex_);

    if (!sameScene(before, scene_)) {
        pushSceneCommand(
            before,
            scene_,
            beforeSelection,
            currentSelectionToken(),
            QStringLiteral("Updated %1").arg(object.name),
            RenderWidget::SceneUpdateMode::TransformsOnly);
        markSceneDirty(QStringLiteral("Updated %1").arg(object.name));
    }
}

void MainWindow::beginSceneEditSession(const QString& description, RenderWidget::SceneUpdateMode updateMode) {
    document_->beginEditSession(description, updateMode, currentSelectionToken());
}

void MainWindow::commitSceneEditSession() {
    document_->commitEditSession(currentSelectionToken());
}

void MainWindow::applySceneState(
    const SceneConfig& scene,
    int selectionToken,
    RenderWidget::SceneUpdateMode updateMode,
    bool resetCamera) {
    applyingSceneState_ = true;
    scene_ = scene;
    for (LightConfig& light : scene_.lights) {
        light = sanitizeLight(light);
    }
    scenegraph::ensureObjectIds(&scene_);

    const int selectedLightIndex = lightIndexFromSelectionToken(selectionToken);
    if (selectedLightIndex >= 0 && selectedLightIndex < scene_.lights.size()) {
        currentLightIndex_ = selectedLightIndex;
        currentObjectIndex_ = -1;
        selectedObjectIndices_.clear();
    } else if (selectionToken >= 0) {
        currentLightIndex_ = -1;
        currentObjectIndex_ = scene_.objects.isEmpty()
            ? -1
            : qBound(0, selectionToken < 0 ? 0 : selectionToken, scene_.objects.size() - 1);
        selectedObjectIndices_ = currentObjectIndex_ >= 0 ? QVector<int>{currentObjectIndex_} : QVector<int>{};
    } else {
        currentLightIndex_ = -1;
        currentObjectIndex_ = -1;
        selectedObjectIndices_.clear();
    }

    renderWidget_->setScene(scene_, updateMode, resetCamera);
    refreshMaterialList();
    refreshLightsPanel();
    refreshToolsPanel();
    refreshObjectTree();
    applyingSceneState_ = false;
    updateWindowCaption();
}

void MainWindow::pushSceneCommand(
    const SceneConfig& before,
    const SceneConfig& after,
    int beforeSelection,
    int afterSelection,
    const QString& description,
    RenderWidget::SceneUpdateMode updateMode) {
    if (sameScene(before, after)) {
        return;
    }

    document_->pushSceneCommand(before, after, beforeSelection, afterSelection, description, updateMode);
}

void MainWindow::applyViewportTransformPreview(
    int index,
    const QVector3D& position,
    const QVector3D& rotationDegrees,
    const QVector3D& scale) {
    if (index < 0 || index >= scene_.objects.size()) {
        return;
    }

    scene_.objects[index].position = position;
    scene_.objects[index].rotationDegrees = rotationDegrees;
    scene_.objects[index].scale = scale;
    if (!selectedObjectIndices_.contains(index)) {
        selectedObjectIndices_ = {index};
    }
    currentObjectIndex_ = index;

    const bool previousSyncState = syncingInspector_;
    syncingInspector_ = true;
    setVectorEditors(positionEdits_, position);
    setVectorEditors(rotationEdits_, rotationDegrees);
    setVectorEditors(scaleEdits_, scale);
    syncingInspector_ = previousSyncState;

    document_->markDirty();
    updateWindowCaption();
}

void MainWindow::applyViewportLightTransformPreview(
    int index,
    const QVector3D& position,
    const QVector3D& direction) {
    if (index < 0 || index >= scene_.lights.size()) {
        return;
    }

    LightConfig& light = scene_.lights[index];
    light.position = position;
    light.direction = sanitizeLightDirection(direction);
    light = sanitizeLight(light);
    currentLightIndex_ = index;
    currentObjectIndex_ = -1;
    selectedObjectIndices_.clear();

    const bool previousSyncState = syncingLights_;
    syncingLights_ = true;
    setVectorEditors(lightPositionEdits_, light.position);
    setVectorEditors(lightDirectionEdits_, light.direction);
    syncingLights_ = previousSyncState;
    if (objectTree_) {
        QTreeWidgetItem* currentItem = objectTree_->currentItem();
        if (currentItem &&
            currentItem->data(0, kSceneTreeItemTypeRole).toInt() == static_cast<int>(SceneTreeItemType::Light) &&
            currentItem->data(0, kSceneTreeItemIndexRole).toInt() == currentLightIndex_) {
            currentItem->setText(0, lightDisplayName(scene_.lights.at(currentLightIndex_), currentLightIndex_));
        }
    }

    document_->markDirty();
    updateWindowCaption();
}

void MainWindow::commitViewportTransform(
    int index,
    const QVector3D& position,
    const QVector3D& rotationDegrees,
    const QVector3D& scale,
    RenderWidget::TransformMode mode) {
    applyViewportTransformPreview(index, position, rotationDegrees, scale);
    commitSceneEditSession();
    statusBar()->showMessage(
        QStringLiteral("%1 %2").arg(transformModeLabel(mode), scene_.objects.at(index).name),
        1200);
}

void MainWindow::commitViewportLightTransform(
    int index,
    const QVector3D& position,
    const QVector3D& direction,
    RenderWidget::TransformMode mode) {
    applyViewportLightTransformPreview(index, position, direction);
    commitSceneEditSession();
    if (index < 0 || index >= scene_.lights.size()) {
        return;
    }

    statusBar()->showMessage(
        QStringLiteral("%1 %2").arg(transformModeLabel(mode), lightDisplayName(scene_.lights.at(index), index)),
        1200);
}

void MainWindow::updateCameraPanel(const QVector3D& position, const QVector3D& target, float distance) {
    if (!cameraPositionLabel_) {
        return;
    }

    cameraPositionLabel_->setText(formatVector(position));
    cameraTargetLabel_->setText(formatVector(target));
    cameraDistanceLabel_->setText(QString::number(distance, 'f', 2));
}

void MainWindow::setTransformMode(RenderWidget::TransformMode mode) {
    if (transformModeCombo_) {
        QSignalBlocker blocker(transformModeCombo_);
        const int index = transformModeCombo_->findData(static_cast<int>(mode));
        transformModeCombo_->setCurrentIndex(index < 0 ? 0 : index);
    }

    if (renderWidget_->transformMode() != mode) {
        renderWidget_->setTransformMode(mode);
    }
}

void MainWindow::setCoordinateSpace(RenderWidget::CoordinateSpace space) {
    if (coordinateSpaceCombo_) {
        QSignalBlocker blocker(coordinateSpaceCombo_);
        const int index = coordinateSpaceCombo_->findData(static_cast<int>(space));
        coordinateSpaceCombo_->setCurrentIndex(index < 0 ? 0 : index);
    }

    if (renderWidget_->coordinateSpace() != space) {
        renderWidget_->setCoordinateSpace(space);
    }
}

void MainWindow::addCubeObject() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    const SceneEditResult result = SceneEditorService::addCube(&scene_, editorSelectionState());
    if (!result.changed) {
        return;
    }

    applyEditorSelection(result.selection);
    renderWidget_->setScene(scene_, toRenderUpdateMode(result.updateImpact));
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentSelectionToken(),
        result.description,
        toRenderUpdateMode(result.updateImpact));
    markSceneDirty(result.description);
}

void MainWindow::addLight() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    const SceneEditResult result = SceneEditorService::addLight(&scene_, editorSelectionState());
    if (!result.changed) {
        return;
    }

    applyEditorSelection(result.selection);
    renderWidget_->setScene(scene_, toRenderUpdateMode(result.updateImpact));
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentSelectionToken(),
        result.description,
        toRenderUpdateMode(result.updateImpact));
    markSceneDirty(result.description);
}

void MainWindow::importModelObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Import Model"),
        QFileInfo(document_->scenePath()).absolutePath(),
        QStringLiteral("Model Files (*.obj *.gltf *.glb)"));
    if (files.isEmpty()) {
        return;
    }

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    QVector<int> insertedIndices;

    for (int fileIndex = 0; fileIndex < files.size(); ++fileIndex) {
        const QString filePath = files.at(fileIndex);
        ModelImportData imported;
        try {
            imported = ModelLoader::importModel(filePath);
        } catch (const std::exception& exception) {
            QMessageBox::warning(
                this,
                QStringLiteral("Import Failed"),
                QStringLiteral("Failed to import %1\n%2")
                    .arg(QFileInfo(filePath).fileName(), QString::fromUtf8(exception.what())));
            continue;
        }

        RenderObjectConfig object;
        object.id = scenegraph::generateObjectId();
        object.name = scenegraph::uniqueObjectName(scene_, QFileInfo(filePath).completeBaseName());
        object.geometry = GeometryType::Model;
        object.sourcePath = normalizeStoredPath(filePath);

        const QString materialBaseName = QFileInfo(filePath).completeBaseName().isEmpty()
            ? QStringLiteral("model")
            : QFileInfo(filePath).completeBaseName();
        object.materialIds.reserve(imported.materials.size());
        for (int materialIndex = 0; materialIndex < imported.materials.size(); ++materialIndex) {
            object.materialIds.append(
                ensureImportedMaterial(&scene_, imported.materials.at(materialIndex), materialBaseName, materialIndex));
        }

        if (currentObjectIndex_ >= 0 && currentObjectIndex_ < scene_.objects.size()) {
            object.parentId = scene_.objects.at(currentObjectIndex_).parentId;
            object.position = scene_.objects.at(currentObjectIndex_).position +
                QVector3D(1.8f * static_cast<float>(fileIndex + 1), 0.0f, 0.8f * static_cast<float>(fileIndex + 1));
        }

        scene_.objects.append(object);
        insertedIndices.append(scene_.objects.size() - 1);
    }

    if (insertedIndices.isEmpty()) {
        return;
    }

    currentObjectIndex_ = insertedIndices.constLast();
    selectedObjectIndices_ = insertedIndices;
    currentLightIndex_ = -1;
    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::ReloadResources);
    refreshMaterialList();
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentSelectionToken(),
        QStringLiteral("Imported %1 model(s)").arg(insertedIndices.size()),
        RenderWidget::SceneUpdateMode::ReloadResources);
    markSceneDirty(QStringLiteral("Imported %1 model(s)").arg(insertedIndices.size()));
}

void MainWindow::duplicateSelectedObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    const SceneEditResult result =
        SceneEditorService::duplicateSelection(&scene_, editorSelectionState(), kDuplicateOffset);
    if (!result.changed) {
        return;
    }

    applyEditorSelection(result.selection);
    const RenderWidget::SceneUpdateMode updateMode = toRenderUpdateMode(result.updateImpact);
    renderWidget_->setScene(scene_, updateMode);
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentSelectionToken(),
        result.description,
        updateMode);
    markSceneDirty(result.description);
}

void MainWindow::copySelectedObjects() {
    const SceneCopyResult copy = SceneEditorService::copySelection(scene_, editorSelectionState());
    clipboardObjects_ = copy.clipboardObjects;
    statusBar()->showMessage(
        clipboardObjects_.isEmpty()
            ? QStringLiteral("Nothing copied")
            : QStringLiteral("Copied %1 object(s)").arg(copy.rootCount),
        1500);
}

void MainWindow::pasteObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();
    if (clipboardObjects_.isEmpty()) {
        return;
    }

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    const SceneEditResult result =
        SceneEditorService::pasteClipboard(&scene_, clipboardObjects_, kDuplicateOffset);
    if (!result.changed) {
        return;
    }

    applyEditorSelection(result.selection);
    const RenderWidget::SceneUpdateMode updateMode = toRenderUpdateMode(result.updateImpact);
    renderWidget_->setScene(scene_, updateMode);
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentSelectionToken(),
        result.description,
        updateMode);
    markSceneDirty(result.description);
}

void MainWindow::deleteSelectedObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();
    if (currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size()) {
        removeSelectedLight();
        return;
    }

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    const SceneEditResult result = SceneEditorService::deleteSelection(&scene_, editorSelectionState());
    if (!result.changed) {
        return;
    }

    applyEditorSelection(result.selection);
    renderWidget_->setScene(scene_, toRenderUpdateMode(result.updateImpact));
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentSelectionToken(),
        result.description,
        toRenderUpdateMode(result.updateImpact));
    markSceneDirty(result.description);
}

void MainWindow::removeSelectedLight() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const SceneConfig before = scene_;
    const int beforeSelection = currentSelectionToken();
    const SceneEditResult result = SceneEditorService::removeSelectedLight(&scene_, editorSelectionState());
    if (!result.changed) {
        return;
    }

    applyEditorSelection(result.selection);
    renderWidget_->setScene(scene_, toRenderUpdateMode(result.updateImpact));
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentSelectionToken(),
        result.description,
        toRenderUpdateMode(result.updateImpact));
    markSceneDirty(result.description);
}

bool MainWindow::saveScene() {
    commitInspectorTransformEdits();
    commitLightEdits();
    scenegraph::ensureObjectIds(&scene_);

    try {
        document_->save();
        updateWindowCaption();
        statusBar()->showMessage(
            QStringLiteral("Saved %1").arg(QFileInfo(document_->scenePath()).fileName()),
            3000);
        return true;
    } catch (const std::exception& exception) {
        QMessageBox::critical(this, QStringLiteral("Save Error"), QString::fromUtf8(exception.what()));
        return false;
    }
}

void MainWindow::reloadScene() {
    commitInspectorTransformEdits();
    commitLightEdits();
    if (!maybeSaveChanges(QStringLiteral("reload the scene from disk"))) {
        return;
    }

    try {
        const int selectionToken = currentSelectionToken();
        document_->reload(selectionToken);
        updateWindowCaption();
        statusBar()->showMessage(QStringLiteral("Reloaded scene"), 3000);
    } catch (const std::exception& exception) {
        QMessageBox::critical(this, QStringLiteral("Reload Error"), QString::fromUtf8(exception.what()));
    }
}

bool MainWindow::maybeSaveChanges(const QString& actionText) {
    if (!document_->hasUnsavedChanges()) {
        return true;
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved Changes"),
        QStringLiteral("The current scene has unsaved changes. Save before you %1?").arg(actionText),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (result == QMessageBox::Cancel) {
        return false;
    }
    if (result == QMessageBox::Save) {
        return saveScene();
    }

    return true;
}

void MainWindow::updateWindowCaption() {
    setWindowTitle(document_->windowCaption());
}

void MainWindow::markSceneDirty(const QString& reason) {
    document_->markDirty();
    updateWindowCaption();
    statusBar()->showMessage(reason, 1500);
}

SceneSelectionState MainWindow::editorSelectionState() const {
    SceneSelectionState selectionState;
    selectionState.selectedObjectIndices = selectedObjectIndices_;
    selectionState.currentObjectIndex = currentObjectIndex_;
    selectionState.currentLightIndex = currentLightIndex_;
    return selectionState;
}

void MainWindow::applyEditorSelection(const SceneSelectionState& selectionState) {
    selectedObjectIndices_ = selectionState.selectedObjectIndices;
    currentObjectIndex_ = selectionState.currentObjectIndex;
    currentLightIndex_ = selectionState.currentLightIndex;
}

RenderWidget::SceneUpdateMode MainWindow::toRenderUpdateMode(SceneUpdateImpact updateImpact) {
    return updateImpact == SceneUpdateImpact::ReloadResources
        ? RenderWidget::SceneUpdateMode::ReloadResources
        : RenderWidget::SceneUpdateMode::TransformsOnly;
}

}  // namespace renderer
