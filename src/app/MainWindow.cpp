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
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>

#include "app/ModelLoader.hpp"
#include "app/SceneGraph.hpp"

namespace {

constexpr int kLayoutVersion = 1;
const char kSettingsOrganization[] = "OpenGLStudy";
const char kSettingsApplication[] = "QtRendererEditor";
const QVector3D kDuplicateOffset(1.5f, 0.0f, 1.5f);

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

QDoubleSpinBox* createSpinBox(double minimum, double maximum, double step) {
    auto* spinBox = new QDoubleSpinBox;
    spinBox->setRange(minimum, maximum);
    spinBox->setSingleStep(step);
    spinBox->setDecimals(3);
    spinBox->setAccelerated(true);
    return spinBox;
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

QWidget* createVectorEditorRow(const QString& label, QDoubleSpinBox* editors[3], QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(new QLabel(label, row));

    for (int index = 0; index < 3; ++index) {
        layout->addWidget(editors[index]);
    }

    return row;
}

void configureDock(QDockWidget* dock) {
    dock->setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable |
        QDockWidget::DockWidgetClosable);
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

int clampSelectedIndex(const renderer::SceneConfig& scene, int index) {
    if (scene.objects.isEmpty()) {
        return -1;
    }

    return qBound(0, index < 0 ? 0 : index, scene.objects.size() - 1);
}

renderer::RenderWidget::SceneUpdateMode mergeUpdateMode(
    renderer::RenderWidget::SceneUpdateMode left,
    renderer::RenderWidget::SceneUpdateMode right) {
    return left == renderer::RenderWidget::SceneUpdateMode::ReloadResources ||
            right == renderer::RenderWidget::SceneUpdateMode::ReloadResources
        ? renderer::RenderWidget::SceneUpdateMode::ReloadResources
        : renderer::RenderWidget::SceneUpdateMode::TransformsOnly;
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

QVector<int> topLevelSelectedIndices(const renderer::SceneConfig& scene, const QVector<int>& selection) {
    const QVector<int> normalized = normalizeSelectionIndices(scene, selection);
    if (normalized.isEmpty()) {
        return {};
    }

    const QHash<QString, int> idLookup = renderer::scenegraph::buildIdLookup(scene);
    QSet<QString> selectedIds;
    for (int index : normalized) {
        selectedIds.insert(scene.objects.at(index).id);
    }

    QVector<int> roots;
    for (int index : normalized) {
        QString parentId = scene.objects.at(index).parentId;
        bool childOfSelection = false;
        while (!parentId.isEmpty() && idLookup.contains(parentId)) {
            if (selectedIds.contains(parentId)) {
                childOfSelection = true;
                break;
            }
            parentId = scene.objects.at(idLookup.value(parentId)).parentId;
        }
        if (!childOfSelection) {
            roots.append(index);
        }
    }

    return roots;
}

QVector<int> collectSubtreeSelection(const renderer::SceneConfig& scene, const QVector<int>& roots) {
    QVector<int> collected;
    QSet<int> visited;
    for (int rootIndex : roots) {
        for (int index : renderer::scenegraph::collectSubtree(scene, rootIndex)) {
            if (visited.contains(index)) {
                continue;
            }
            visited.insert(index);
            collected.append(index);
        }
    }
    std::sort(collected.begin(), collected.end());
    return collected;
}

QVector<renderer::RenderObjectConfig> collectClipboardObjects(
    const renderer::SceneConfig& scene,
    const QVector<int>& roots,
    bool clearExternalParents) {
    QVector<renderer::RenderObjectConfig> objects;
    QSet<QString> copiedIds;
    for (int rootIndex : roots) {
        for (int index : renderer::scenegraph::collectSubtree(scene, rootIndex)) {
            objects.append(scene.objects.at(index));
            copiedIds.insert(scene.objects.at(index).id);
        }
    }

    if (!clearExternalParents) {
        return objects;
    }

    for (renderer::RenderObjectConfig& object : objects) {
        if (!object.parentId.isEmpty() && !copiedIds.contains(object.parentId)) {
            object.parentId.clear();
        }
    }
    return objects;
}

QVector<int> appendClonedObjects(
    renderer::SceneConfig* scene,
    const QVector<renderer::RenderObjectConfig>& prototypes,
    bool preserveExternalParents,
    const QVector3D& rootOffset) {
    QVector<int> inserted;
    if (!scene || prototypes.isEmpty()) {
        return inserted;
    }

    QHash<QString, QString> remappedIds;
    QSet<QString> internalIds;
    for (const renderer::RenderObjectConfig& prototype : prototypes) {
        internalIds.insert(prototype.id);
    }

    for (const renderer::RenderObjectConfig& prototype : prototypes) {
        renderer::RenderObjectConfig clone = prototype;
        const QString originalId = clone.id;
        const QString originalParentId = clone.parentId;
        clone.id = renderer::scenegraph::generateObjectId();
        remappedIds.insert(originalId, clone.id);
        clone.name = renderer::scenegraph::uniqueObjectName(
            *scene,
            clone.name.isEmpty() ? QStringLiteral("object") : clone.name);

        if (!originalParentId.isEmpty() && remappedIds.contains(originalParentId)) {
            clone.parentId = remappedIds.value(originalParentId);
        } else if (!preserveExternalParents || originalParentId.isEmpty() || !internalIds.contains(originalParentId)) {
            clone.parentId = preserveExternalParents ? originalParentId : QString();
        }

        if (originalParentId.isEmpty() || !internalIds.contains(originalParentId)) {
            clone.position += rootOffset;
        }

        scene->objects.append(clone);
        inserted.append(scene->objects.size() - 1);
    }

    return inserted;
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

bool selectionIncludesModel(const renderer::SceneConfig& scene, const QVector<int>& selection) {
    for (int index : selection) {
        if (index >= 0 && index < scene.objects.size() && scene.objects.at(index).geometry == renderer::GeometryType::Model) {
            return true;
        }
    }
    return false;
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

MainWindow::MainWindow(const SceneConfig& scene, const QString& scenePath, QWidget* parent)
    : QMainWindow(parent),
      scenePath_(scenePath),
      scene_(scene),
      undoStack_(new QUndoStack(this)) {
    scenegraph::ensureObjectIds(&scene_);
    for (LightConfig& light : scene_.lights) {
        light = sanitizeLight(light);
    }
    resize(scene_.window.size.expandedTo(QSize(1280, 820)));
    setMinimumSize(1024, 720);
    setDockNestingEnabled(true);
    setDockOptions(
        QMainWindow::AnimatedDocks |
        QMainWindow::AllowNestedDocks |
        QMainWindow::AllowTabbedDocks);

    createDocks();
    createMenus();

    connect(undoStack_, &QUndoStack::indexChanged, this, [this](int) {
        dirty_ = !undoStack_->isClean() || sceneEditSessionActive_;
        updateWindowCaption();
    });

    connect(renderWidget_, &RenderWidget::selectionChanged, this, [this](const QVector<int>& indices, int activeIndex) {
        setSelectionState(indices, activeIndex);
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
    } else {
        updateCameraPanel(scene_.camera.position, scene_.camera.position + QVector3D(0.0f, 0.0f, -10.0f), 10.0f);
    }

    undoStack_->clear();
    undoStack_->setClean();
    dirty_ = false;
    updateWindowCaption();
    statusBar()->showMessage(QStringLiteral("Scene loaded from %1").arg(QFileInfo(scenePath_).fileName()), 3000);
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

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    auto* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    auto* cameraMenu = menuBar()->addMenu(QStringLiteral("Camera"));
    auto* panelsMenu = menuBar()->addMenu(QStringLiteral("Panels"));

    auto* saveAction = fileMenu->addAction(QStringLiteral("Save Scene"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() {
        saveScene();
    });

    auto* reloadAction = fileMenu->addAction(QStringLiteral("Reload Scene"));
    reloadAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(reloadAction, &QAction::triggered, this, [this]() {
        reloadScene();
    });

    fileMenu->addSeparator();
    auto* importAction = fileMenu->addAction(QStringLiteral("Import Model"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, &MainWindow::importModelObjects);

    auto* undoAction = undoStack_->createUndoAction(this, QStringLiteral("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    auto* redoAction = undoStack_->createRedoAction(this, QStringLiteral("Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();

    auto* copyAction = editMenu->addAction(QStringLiteral("Copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &MainWindow::copySelectedObjects);

    auto* pasteAction = editMenu->addAction(QStringLiteral("Paste"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::pasteObjects);

    auto* duplicateAction = editMenu->addAction(QStringLiteral("Duplicate"));
    duplicateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(duplicateAction, &QAction::triggered, this, &MainWindow::duplicateSelectedObjects);

    editMenu->addSeparator();

    auto* addAction = editMenu->addAction(QStringLiteral("Add Cube"));
    addAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(addAction, &QAction::triggered, this, [this]() {
        addCubeObject();
    });

    auto* deleteAction = editMenu->addAction(QStringLiteral("Delete Selected"));
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, [this]() {
        deleteSelectedObjects();
    });

    auto* focusAction = cameraMenu->addAction(QStringLiteral("Frame Selection"));
    focusAction->setShortcut(QKeySequence(QStringLiteral("F")));
    connect(focusAction, &QAction::triggered, renderWidget_, &RenderWidget::focusOnSelectedObject);

    auto* resetAction = cameraMenu->addAction(QStringLiteral("Reset Camera"));
    resetAction->setShortcut(QKeySequence(QStringLiteral("Home")));
    connect(resetAction, &QAction::triggered, renderWidget_, &RenderWidget::resetCamera);

    auto* resetLayoutAction = panelsMenu->addAction(QStringLiteral("Restore Default Layout"));
    connect(resetLayoutAction, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);

    auto* showPanelsAction = panelsMenu->addAction(QStringLiteral("Show All Windows"));
    connect(showPanelsAction, &QAction::triggered, this, &MainWindow::showAllPanels);

    panelsMenu->addSeparator();
    panelsMenu->addAction(sceneDock_->toggleViewAction());
    panelsMenu->addAction(inspectorDock_->toggleViewAction());
    panelsMenu->addAction(materialDock_->toggleViewAction());
    panelsMenu->addAction(lightsDock_->toggleViewAction());
    panelsMenu->addAction(cameraDock_->toggleViewAction());
    panelsMenu->addAction(toolsDock_->toggleViewAction());

    auto* toolbar = addToolBar(QStringLiteral("Scene"));
    toolbar->setMovable(false);
    toolbar->addAction(saveAction);
    toolbar->addAction(reloadAction);
    toolbar->addAction(importAction);
    toolbar->addSeparator();
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);
    toolbar->addAction(copyAction);
    toolbar->addAction(pasteAction);
    toolbar->addAction(duplicateAction);
    toolbar->addSeparator();
    toolbar->addAction(addAction);
    toolbar->addAction(deleteAction);
    toolbar->addSeparator();
    toolbar->addAction(focusAction);
    toolbar->addAction(resetAction);
    toolbar->addSeparator();
    toolbar->addAction(resetLayoutAction);
}

void MainWindow::createDocks() {
    renderWidget_ = new RenderWidget(scene_, this);
    setCentralWidget(renderWidget_);

    sceneDock_ = new QDockWidget(QStringLiteral("Scene"), this);
    configureDock(sceneDock_);
    sceneDock_->setObjectName(QStringLiteral("SceneDock"));
    sceneDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    objectTree_ = new QTreeWidget(sceneDock_);
    objectTree_->setHeaderHidden(true);
    objectTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    objectTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(objectTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::handleObjectTreeSelectionChanged);
    sceneDock_->setWidget(objectTree_);
    addDockWidget(Qt::LeftDockWidgetArea, sceneDock_);

    inspectorDock_ = new QDockWidget(QStringLiteral("Inspector"), this);
    configureDock(inspectorDock_);
    inspectorDock_->setObjectName(QStringLiteral("InspectorDock"));
    inspectorDock_->setAllowedAreas(Qt::AllDockWidgetAreas);

    auto* inspectorWidget = new QWidget(inspectorDock_);
    auto* inspectorLayout = new QVBoxLayout(inspectorWidget);
    inspectorLayout->setContentsMargins(12, 12, 12, 12);
    inspectorLayout->setSpacing(12);

    auto* generalGroup = new QGroupBox(QStringLiteral("Object"), inspectorWidget);
    auto* generalLayout = new QFormLayout(generalGroup);
    nameEdit_ = new QLineEdit(generalGroup);
    materialCombo_ = new QComboBox(generalGroup);
    materialCombo_->setEditable(true);
    visibleCheck_ = new QCheckBox(QStringLiteral("Visible"), generalGroup);
    generalLayout->addRow(QStringLiteral("Name"), nameEdit_);
    generalLayout->addRow(QStringLiteral("Material"), materialCombo_);
    generalLayout->addRow(QString(), visibleCheck_);

    auto* transformGroup = new QGroupBox(QStringLiteral("Transform"), inspectorWidget);
    auto* transformLayout = new QVBoxLayout(transformGroup);
    transformLayout->setContentsMargins(12, 12, 12, 12);

    for (int index = 0; index < 3; ++index) {
        positionEdits_[index] = createSpinBox(-1000.0, 1000.0, 0.1);
        rotationEdits_[index] = createSpinBox(-360.0, 360.0, 1.0);
        scaleEdits_[index] = createSpinBox(0.01, 100.0, 0.1);
    }

    transformLayout->addWidget(createVectorEditorRow(QStringLiteral("Position"), positionEdits_, transformGroup));
    transformLayout->addWidget(createVectorEditorRow(QStringLiteral("Rotation"), rotationEdits_, transformGroup));
    transformLayout->addWidget(createVectorEditorRow(QStringLiteral("Scale"), scaleEdits_, transformGroup));

    inspectorLayout->addWidget(generalGroup);
    inspectorLayout->addWidget(transformGroup);
    inspectorLayout->addStretch(1);
    inspectorDock_->setWidget(inspectorWidget);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);

    materialDock_ = new QDockWidget(QStringLiteral("Materials"), this);
    configureDock(materialDock_);
    materialDock_->setObjectName(QStringLiteral("MaterialDock"));
    materialDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    materialList_ = new QListWidget(materialDock_);
    materialList_->setSelectionMode(QAbstractItemView::NoSelection);
    materialDock_->setWidget(materialList_);
    addDockWidget(Qt::BottomDockWidgetArea, materialDock_);

    lightsDock_ = new QDockWidget(QStringLiteral("Lights"), this);
    configureDock(lightsDock_);
    lightsDock_->setObjectName(QStringLiteral("LightsDock"));
    lightsDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    createLightsPanel(lightsDock_);
    addDockWidget(Qt::BottomDockWidgetArea, lightsDock_);
    tabifyDockWidget(materialDock_, lightsDock_);

    cameraDock_ = new QDockWidget(QStringLiteral("Camera"), this);
    configureDock(cameraDock_);
    cameraDock_->setObjectName(QStringLiteral("CameraDock"));
    cameraDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    createCameraPanel(cameraDock_);
    addDockWidget(Qt::RightDockWidgetArea, cameraDock_);

    toolsDock_ = new QDockWidget(QStringLiteral("Tools"), this);
    configureDock(toolsDock_);
    toolsDock_->setObjectName(QStringLiteral("ToolsDock"));
    toolsDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    createToolsPanel(toolsDock_);
    addDockWidget(Qt::RightDockWidgetArea, toolsDock_);

    connect(nameEdit_, &QLineEdit::editingFinished, this, &MainWindow::applyInspectorMetadataEdits);
    connect(materialCombo_, &QComboBox::currentTextChanged, this, [this](const QString&) {
        applyInspectorMetadataEdits();
    });
    connect(visibleCheck_, &QCheckBox::toggled, this, [this](bool) {
        applyInspectorMetadataEdits();
    });

    for (int index = 0; index < 3; ++index) {
        connect(positionEdits_[index], qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            previewInspectorTransformEdits();
        });
        connect(rotationEdits_[index], qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            previewInspectorTransformEdits();
        });
        connect(scaleEdits_[index], qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            previewInspectorTransformEdits();
        });
        connect(positionEdits_[index], &QDoubleSpinBox::editingFinished, this, &MainWindow::commitInspectorTransformEdits);
        connect(rotationEdits_[index], &QDoubleSpinBox::editingFinished, this, &MainWindow::commitInspectorTransformEdits);
        connect(scaleEdits_[index], &QDoubleSpinBox::editingFinished, this, &MainWindow::commitInspectorTransformEdits);
    }
}

void MainWindow::createLightsPanel(QDockWidget* dock) {
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    lightList_ = new QListWidget(panel);
    lightList_->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* actionsRow = new QWidget(panel);
    auto* actionsLayout = new QHBoxLayout(actionsRow);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    addLightButton_ = new QPushButton(QStringLiteral("Add Light"), actionsRow);
    removeLightButton_ = new QPushButton(QStringLiteral("Remove Light"), actionsRow);
    actionsLayout->addWidget(addLightButton_);
    actionsLayout->addWidget(removeLightButton_);

    auto* lightGroup = new QGroupBox(QStringLiteral("Light"), panel);
    auto* lightLayout = new QFormLayout(lightGroup);
    lightTypeCombo_ = new QComboBox(lightGroup);
    lightTypeCombo_->addItem(QStringLiteral("Directional"), static_cast<int>(LightType::Directional));
    lightTypeCombo_->addItem(QStringLiteral("Point"), static_cast<int>(LightType::Point));
    lightTypeCombo_->addItem(QStringLiteral("Spot"), static_cast<int>(LightType::Spot));
    lightLayout->addRow(QStringLiteral("Type"), lightTypeCombo_);

    for (int index = 0; index < 3; ++index) {
        lightPositionEdits_[index] = createSpinBox(-1000.0, 1000.0, 0.1);
        lightDirectionEdits_[index] = createSpinBox(-1.0, 1.0, 0.05);
        lightDirectionEdits_[index]->setDecimals(3);
        lightColorEdits_[index] = createSpinBox(0.0, 10.0, 0.05);
        lightColorEdits_[index]->setDecimals(2);
    }

    lightAmbientEdit_ = createSpinBox(0.0, 2.0, 0.01);
    lightAmbientEdit_->setDecimals(2);
    lightIntensityEdit_ = createSpinBox(0.0, 20.0, 0.1);
    lightIntensityEdit_->setDecimals(2);
    lightRangeEdit_ = createSpinBox(0.1, 500.0, 0.5);
    lightRangeEdit_->setDecimals(2);
    lightInnerConeEdit_ = createSpinBox(0.1, 89.0, 1.0);
    lightInnerConeEdit_->setDecimals(1);
    lightOuterConeEdit_ = createSpinBox(0.1, 89.5, 1.0);
    lightOuterConeEdit_->setDecimals(1);

    lightLayout->addRow(QStringLiteral("Position"), createVectorEditorRow(QString(), lightPositionEdits_, lightGroup));
    lightLayout->addRow(QStringLiteral("Direction"), createVectorEditorRow(QString(), lightDirectionEdits_, lightGroup));
    lightLayout->addRow(QStringLiteral("Color"), createVectorEditorRow(QString(), lightColorEdits_, lightGroup));
    lightLayout->addRow(QStringLiteral("Ambient"), lightAmbientEdit_);
    lightLayout->addRow(QStringLiteral("Intensity"), lightIntensityEdit_);
    lightLayout->addRow(QStringLiteral("Range"), lightRangeEdit_);
    lightLayout->addRow(QStringLiteral("Inner Cone"), lightInnerConeEdit_);
    lightLayout->addRow(QStringLiteral("Outer Cone"), lightOuterConeEdit_);

    layout->addWidget(lightList_);
    layout->addWidget(actionsRow);
    layout->addWidget(lightGroup);
    layout->addStretch(1);

    connect(lightList_, &QListWidget::currentRowChanged, this, [this](int) {
        handleLightSelectionChanged();
    });
    connect(addLightButton_, &QPushButton::clicked, this, &MainWindow::addLight);
    connect(removeLightButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedLight);
    connect(lightTypeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (syncingLights_ || applyingSceneState_) {
            return;
        }
        previewLightEdits();
        commitLightEdits();
    });

    const auto connectLightEdit = [this](QDoubleSpinBox* spinBox) {
        connect(spinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            previewLightEdits();
        });
        connect(spinBox, &QDoubleSpinBox::editingFinished, this, &MainWindow::commitLightEdits);
    };

    for (int index = 0; index < 3; ++index) {
        connectLightEdit(lightPositionEdits_[index]);
        connectLightEdit(lightDirectionEdits_[index]);
        connectLightEdit(lightColorEdits_[index]);
    }
    connectLightEdit(lightAmbientEdit_);
    connectLightEdit(lightIntensityEdit_);
    connectLightEdit(lightRangeEdit_);
    connectLightEdit(lightInnerConeEdit_);
    connectLightEdit(lightOuterConeEdit_);

    dock->setWidget(panel);
}

void MainWindow::createCameraPanel(QDockWidget* dock) {
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* statusGroup = new QGroupBox(QStringLiteral("State"), panel);
    auto* statusLayout = new QFormLayout(statusGroup);
    cameraPositionLabel_ = new QLabel(statusGroup);
    cameraTargetLabel_ = new QLabel(statusGroup);
    cameraDistanceLabel_ = new QLabel(statusGroup);
    cameraPositionLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cameraTargetLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cameraDistanceLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addRow(QStringLiteral("Position"), cameraPositionLabel_);
    statusLayout->addRow(QStringLiteral("Target"), cameraTargetLabel_);
    statusLayout->addRow(QStringLiteral("Distance"), cameraDistanceLabel_);

    auto* actionRow = new QWidget(panel);
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    auto* focusButton = new QPushButton(QStringLiteral("Frame Selection"), actionRow);
    auto* resetButton = new QPushButton(QStringLiteral("Reset Camera"), actionRow);
    actionLayout->addWidget(focusButton);
    actionLayout->addWidget(resetButton);

    auto* helpLabel = new QLabel(
        QStringLiteral(
            "Controls\n"
            "W / E / R: move / rotate / scale mode\n"
            "Left Click: select or use gizmo\n"
            "Left Drag: marquee select on empty space\n"
            "Right Hold: temporary camera mode\n"
            "Middle Drag: pan\n"
            "Alt + Left Drag: orbit\n"
            "Wheel: zoom\n"
            "Right Hold + W A S D Q E: camera move\n"
            "Shift + Drag: temporary snap\n"
            "F: frame selection\n"
            "Home: reset camera"),
        panel);
    helpLabel->setWordWrap(true);

    layout->addWidget(statusGroup);
    layout->addWidget(actionRow);
    layout->addWidget(helpLabel);
    layout->addStretch(1);

    connect(focusButton, &QPushButton::clicked, renderWidget_, &RenderWidget::focusOnSelectedObject);
    connect(resetButton, &QPushButton::clicked, renderWidget_, &RenderWidget::resetCamera);

    dock->setWidget(panel);
}

void MainWindow::createToolsPanel(QDockWidget* dock) {
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* transformGroup = new QGroupBox(QStringLiteral("Transform"), panel);
    auto* transformLayout = new QFormLayout(transformGroup);
    transformModeCombo_ = new QComboBox(transformGroup);
    transformModeCombo_->addItem(QStringLiteral("Move (W)"), static_cast<int>(RenderWidget::TransformMode::Translate));
    transformModeCombo_->addItem(QStringLiteral("Rotate (E)"), static_cast<int>(RenderWidget::TransformMode::Rotate));
    transformModeCombo_->addItem(QStringLiteral("Scale (R)"), static_cast<int>(RenderWidget::TransformMode::Scale));
    coordinateSpaceCombo_ = new QComboBox(transformGroup);
    coordinateSpaceCombo_->addItem(QStringLiteral("World"), static_cast<int>(RenderWidget::CoordinateSpace::World));
    coordinateSpaceCombo_->addItem(QStringLiteral("Local"), static_cast<int>(RenderWidget::CoordinateSpace::Local));
    transformLayout->addRow(QStringLiteral("Mode"), transformModeCombo_);
    transformLayout->addRow(QStringLiteral("Space"), coordinateSpaceCombo_);

    auto* snapGroup = new QGroupBox(QStringLiteral("Snap"), panel);
    auto* snapLayout = new QFormLayout(snapGroup);
    snapEnabledCheck_ = new QCheckBox(QStringLiteral("Always Snap"), snapGroup);
    moveSnapStepEdit_ = createSpinBox(0.05, 50.0, 0.25);
    moveSnapStepEdit_->setDecimals(2);
    rotateSnapStepEdit_ = createSpinBox(1.0, 180.0, 1.0);
    rotateSnapStepEdit_->setDecimals(1);
    scaleSnapStepEdit_ = createSpinBox(0.01, 10.0, 0.05);
    scaleSnapStepEdit_->setDecimals(2);
    snapLayout->addRow(QString(), snapEnabledCheck_);
    snapLayout->addRow(QStringLiteral("Move Step"), moveSnapStepEdit_);
    snapLayout->addRow(QStringLiteral("Rotate Step"), rotateSnapStepEdit_);
    snapLayout->addRow(QStringLiteral("Scale Step"), scaleSnapStepEdit_);

    auto* layoutGroup = new QGroupBox(QStringLiteral("Windows"), panel);
    auto* layoutButtons = new QVBoxLayout(layoutGroup);
    layoutButtons->setContentsMargins(12, 12, 12, 12);
    layoutButtons->setSpacing(8);
    auto* restoreLayoutButton = new QPushButton(QStringLiteral("Restore Default Layout"), layoutGroup);
    auto* showWindowsButton = new QPushButton(QStringLiteral("Show All Windows"), layoutGroup);
    layoutButtons->addWidget(restoreLayoutButton);
    layoutButtons->addWidget(showWindowsButton);

    layout->addWidget(transformGroup);
    layout->addWidget(snapGroup);
    layout->addWidget(layoutGroup);
    layout->addStretch(1);

    connect(transformModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (applyingSceneState_ || !transformModeCombo_) {
            return;
        }
        const QVariant data = transformModeCombo_->itemData(index);
        setTransformMode(static_cast<RenderWidget::TransformMode>(data.toInt()));
    });
    connect(coordinateSpaceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (applyingSceneState_ || !coordinateSpaceCombo_) {
            return;
        }
        const QVariant data = coordinateSpaceCombo_->itemData(index);
        setCoordinateSpace(static_cast<RenderWidget::CoordinateSpace>(data.toInt()));
    });
    connect(snapEnabledCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (applyingSceneState_) {
            return;
        }
        const SceneConfig before = scene_;
        scene_.debug.snapEnabled = enabled;
        renderWidget_->setSnapEnabled(enabled);
        if (!sameScene(before, scene_)) {
            pushSceneCommand(
                before,
                scene_,
                currentObjectIndex_,
                currentObjectIndex_,
                QStringLiteral("Updated snap mode"),
                RenderWidget::SceneUpdateMode::TransformsOnly);
            markSceneDirty(QStringLiteral("Updated snap mode"));
        }
    });
    connect(moveSnapStepEdit_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (applyingSceneState_) {
            return;
        }
        const SceneConfig before = scene_;
        scene_.debug.gridStep = static_cast<float>(value);
        renderWidget_->setMoveSnapStep(scene_.debug.gridStep);
        renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::ReloadResources);
        renderWidget_->setSelection(selectedObjectIndices_, currentObjectIndex_);
        if (!sameScene(before, scene_)) {
            pushSceneCommand(
                before,
                scene_,
                currentObjectIndex_,
                currentObjectIndex_,
                QStringLiteral("Updated move snap step"),
                RenderWidget::SceneUpdateMode::ReloadResources);
            markSceneDirty(QStringLiteral("Updated move snap step"));
        }
    });
    connect(rotateSnapStepEdit_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (applyingSceneState_) {
            return;
        }
        const SceneConfig before = scene_;
        scene_.debug.rotateSnapDegrees = static_cast<float>(value);
        renderWidget_->setRotateSnapStep(scene_.debug.rotateSnapDegrees);
        if (!sameScene(before, scene_)) {
            pushSceneCommand(
                before,
                scene_,
                currentObjectIndex_,
                currentObjectIndex_,
                QStringLiteral("Updated rotate snap step"),
                RenderWidget::SceneUpdateMode::TransformsOnly);
            markSceneDirty(QStringLiteral("Updated rotate snap step"));
        }
    });
    connect(scaleSnapStepEdit_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (applyingSceneState_) {
            return;
        }
        const SceneConfig before = scene_;
        scene_.debug.scaleSnapStep = static_cast<float>(value);
        renderWidget_->setScaleSnapStep(scene_.debug.scaleSnapStep);
        if (!sameScene(before, scene_)) {
            pushSceneCommand(
                before,
                scene_,
                currentObjectIndex_,
                currentObjectIndex_,
                QStringLiteral("Updated scale snap step"),
                RenderWidget::SceneUpdateMode::TransformsOnly);
            markSceneDirty(QStringLiteral("Updated scale snap step"));
        }
    });
    connect(restoreLayoutButton, &QPushButton::clicked, this, &MainWindow::restoreDefaultLayout);
    connect(showWindowsButton, &QPushButton::clicked, this, &MainWindow::showAllPanels);

    dock->setWidget(panel);
}

void MainWindow::restoreDefaultLayout() {
    for (QDockWidget* dock : {sceneDock_, inspectorDock_, materialDock_, lightsDock_, cameraDock_, toolsDock_}) {
        dock->show();
        dock->setFloating(false);
    }

    addDockWidget(Qt::LeftDockWidgetArea, sceneDock_);
    addDockWidget(Qt::LeftDockWidgetArea, inspectorDock_);
    splitDockWidget(sceneDock_, inspectorDock_, Qt::Vertical);

    addDockWidget(Qt::RightDockWidgetArea, toolsDock_);
    addDockWidget(Qt::RightDockWidgetArea, cameraDock_);
    splitDockWidget(toolsDock_, cameraDock_, Qt::Vertical);

    addDockWidget(Qt::BottomDockWidgetArea, materialDock_);
    addDockWidget(Qt::BottomDockWidgetArea, lightsDock_);
    tabifyDockWidget(materialDock_, lightsDock_);

    resizeDocks({sceneDock_, toolsDock_}, {300, 320}, Qt::Horizontal);
    resizeDocks({sceneDock_, inspectorDock_}, {340, 320}, Qt::Vertical);
    resizeDocks({toolsDock_, cameraDock_}, {280, 260}, Qt::Vertical);

    statusBar()->showMessage(QStringLiteral("Restored docked editor layout"), 2500);
}

void MainWindow::showAllPanels() {
    for (QDockWidget* dock : {sceneDock_, inspectorDock_, materialDock_, lightsDock_, cameraDock_, toolsDock_}) {
        dock->show();
        dock->raise();
    }

    statusBar()->showMessage(QStringLiteral("All windows are visible"), 2000);
}

bool MainWindow::restoreEditorLayout() {
    QSettings settings(QString::fromLatin1(kSettingsOrganization), QString::fromLatin1(kSettingsApplication));
    if (!settings.contains(QStringLiteral("layout/geometry")) || !settings.contains(QStringLiteral("layout/state"))) {
        return false;
    }

    const QByteArray geometry = settings.value(QStringLiteral("layout/geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("layout/state")).toByteArray();
    restoreGeometry(geometry);
    const bool restored = restoreState(state, kLayoutVersion);
    if (!restored) {
        return false;
    }

    setTransformMode(static_cast<RenderWidget::TransformMode>(
        settings.value(QStringLiteral("editor/transformMode"), static_cast<int>(RenderWidget::TransformMode::Translate)).toInt()));
    setCoordinateSpace(static_cast<RenderWidget::CoordinateSpace>(
        settings.value(QStringLiteral("editor/coordinateSpace"), static_cast<int>(RenderWidget::CoordinateSpace::World)).toInt()));
    return true;
}

void MainWindow::saveEditorLayout() const {
    QSettings settings(QString::fromLatin1(kSettingsOrganization), QString::fromLatin1(kSettingsApplication));
    settings.setValue(QStringLiteral("layout/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("layout/state"), saveState(kLayoutVersion));
    settings.setValue(QStringLiteral("editor/transformMode"), static_cast<int>(renderWidget_->transformMode()));
    settings.setValue(QStringLiteral("editor/coordinateSpace"), static_cast<int>(renderWidget_->coordinateSpace()));
}

void MainWindow::refreshObjectTree() {
    scenegraph::ensureObjectIds(&scene_);

    QSignalBlocker blocker(objectTree_);
    objectTree_->clear();

    std::function<void(QTreeWidgetItem*, int)> appendItem = [&](QTreeWidgetItem* parentItem, int objectIndex) {
        const RenderObjectConfig& object = scene_.objects.at(objectIndex);
        auto* item = new QTreeWidgetItem;
        item->setText(0, objectDisplayName(object, objectIndex));
        item->setData(0, Qt::UserRole, objectIndex);
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

    objectTree_->expandAll();
    setSelectionState(selectedObjectIndices_, currentObjectIndex_);
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
    if (!lightList_) {
        return;
    }

    if (scene_.lights.isEmpty()) {
        currentLightIndex_ = -1;
    } else {
        currentLightIndex_ = qBound(0, currentLightIndex_ < 0 ? 0 : currentLightIndex_, scene_.lights.size() - 1);
    }

    syncingLights_ = true;
    {
        QSignalBlocker blocker(lightList_);
        lightList_->clear();
        for (int index = 0; index < scene_.lights.size(); ++index) {
            lightList_->addItem(lightDisplayName(scene_.lights.at(index), index));
        }
        if (currentLightIndex_ >= 0) {
            lightList_->setCurrentRow(currentLightIndex_);
        }
    }
    syncingLights_ = false;

    refreshLightInspector();
}

void MainWindow::refreshLightInspector() {
    if (!lightTypeCombo_) {
        return;
    }

    const bool hasLight = currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size();
    syncingLights_ = true;

    lightTypeCombo_->setEnabled(hasLight);
    if (addLightButton_) {
        addLightButton_->setEnabled(true);
    }
    if (removeLightButton_) {
        removeLightButton_->setEnabled(hasLight && scene_.lights.size() > 1);
    }

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

    if (objectTree_) {
        QSignalBlocker blocker(objectTree_);
        objectTree_->clearSelection();
        QTreeWidgetItem* currentItem = nullptr;
        for (QTreeWidgetItemIterator it(objectTree_); *it; ++it) {
            auto* item = *it;
            const int itemIndex = item->data(0, Qt::UserRole).toInt();
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

void MainWindow::handleObjectTreeSelectionChanged() {
    QVector<int> indices;
    for (QTreeWidgetItem* item : objectTree_->selectedItems()) {
        indices.append(item->data(0, Qt::UserRole).toInt());
    }

    int currentIndex = -1;
    if (QTreeWidgetItem* currentItem = objectTree_->currentItem()) {
        currentIndex = currentItem->data(0, Qt::UserRole).toInt();
    }

    setSelectionState(indices, currentIndex);
}

void MainWindow::handleLightSelectionChanged() {
    if (syncingLights_) {
        return;
    }

    currentLightIndex_ = lightList_ ? lightList_->currentRow() : -1;
    refreshLightInspector();

    if (currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size()) {
        statusBar()->showMessage(
            QStringLiteral("Selected %1").arg(lightDisplayName(scene_.lights.at(currentLightIndex_), currentLightIndex_)),
            1500);
    }
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
    renderWidget_->setSelection(selectedObjectIndices_, currentObjectIndex_);
    if (lightList_ && currentLightIndex_ < lightList_->count()) {
        lightList_->item(currentLightIndex_)->setText(
            lightDisplayName(scene_.lights.at(currentLightIndex_), currentLightIndex_));
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
    const int beforeSelection = currentObjectIndex_;
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
            currentObjectIndex_,
            QStringLiteral("Updated %1").arg(object.name),
            RenderWidget::SceneUpdateMode::TransformsOnly);
        markSceneDirty(QStringLiteral("Updated %1").arg(object.name));
    }
}

void MainWindow::beginSceneEditSession(const QString& description, RenderWidget::SceneUpdateMode updateMode) {
    if (!sceneEditSessionActive_) {
        sceneEditSessionActive_ = true;
        sceneEditBefore_ = scene_;
        sceneEditBeforeSelection_ = currentObjectIndex_;
        sceneEditDescription_ = description;
        sceneEditUpdateMode_ = updateMode;
        return;
    }

    sceneEditUpdateMode_ = mergeUpdateMode(sceneEditUpdateMode_, updateMode);
}

void MainWindow::commitSceneEditSession() {
    if (!sceneEditSessionActive_) {
        return;
    }

    const SceneConfig before = sceneEditBefore_;
    const int beforeSelection = sceneEditBeforeSelection_;
    const QString description = sceneEditDescription_;
    const RenderWidget::SceneUpdateMode updateMode = sceneEditUpdateMode_;

    sceneEditSessionActive_ = false;
    sceneEditDescription_.clear();
    sceneEditUpdateMode_ = RenderWidget::SceneUpdateMode::TransformsOnly;

    if (sameScene(before, scene_)) {
        dirty_ = !undoStack_->isClean();
        updateWindowCaption();
        return;
    }

    pushSceneCommand(before, scene_, beforeSelection, currentObjectIndex_, description, updateMode);
    markSceneDirty(description);
}

void MainWindow::applySceneState(
    const SceneConfig& scene,
    int selectedObjectIndex,
    RenderWidget::SceneUpdateMode updateMode,
    bool resetCamera) {
    applyingSceneState_ = true;
    scene_ = scene;
    for (LightConfig& light : scene_.lights) {
        light = sanitizeLight(light);
    }
    currentObjectIndex_ = clampSelectedIndex(scene_, selectedObjectIndex);
    selectedObjectIndices_ = currentObjectIndex_ >= 0 ? QVector<int>{currentObjectIndex_} : QVector<int>{};
    scenegraph::ensureObjectIds(&scene_);
    renderWidget_->setScene(scene_, updateMode, resetCamera);
    refreshMaterialList();
    refreshLightsPanel();
    refreshToolsPanel();
    refreshObjectTree();
    applyingSceneState_ = false;
    dirty_ = !undoStack_->isClean() || sceneEditSessionActive_;
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

    undoStack_->push(new LambdaUndoCommand(
        description,
        [this, before, beforeSelection, updateMode]() {
            applySceneState(before, beforeSelection, updateMode);
        },
        [this, after, afterSelection, updateMode]() {
            applySceneState(after, afterSelection, updateMode);
        }));
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

    dirty_ = true;
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
    const int beforeSelection = currentObjectIndex_;

    RenderObjectConfig object;
    object.id = scenegraph::generateObjectId();
    object.name = scenegraph::uniqueObjectName(scene_, QStringLiteral("cube"));
    if (!scene_.materials.isEmpty()) {
        object.materialId = scene_.materials.constFirst().id;
    }
    if (currentObjectIndex_ >= 0 && currentObjectIndex_ < scene_.objects.size()) {
        object.parentId = scene_.objects.at(currentObjectIndex_).parentId;
        object.position = scene_.objects.at(currentObjectIndex_).position + QVector3D(1.5f, 0.0f, 0.0f);
    }

    scene_.objects.append(object);
    currentObjectIndex_ = scene_.objects.size() - 1;
    selectedObjectIndices_ = {currentObjectIndex_};
    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::TransformsOnly);
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentObjectIndex_,
        QStringLiteral("Added %1").arg(object.name),
        RenderWidget::SceneUpdateMode::TransformsOnly);
    markSceneDirty(QStringLiteral("Added %1").arg(object.name));
}

void MainWindow::addLight() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const SceneConfig before = scene_;
    const int beforeSelection = currentObjectIndex_;

    LightConfig light;
    if (currentLightIndex_ >= 0 && currentLightIndex_ < scene_.lights.size()) {
        light = scene_.lights.at(currentLightIndex_);
        light.position += QVector3D(1.5f, 1.0f, 0.0f);
    }
    light = sanitizeLight(light);

    scene_.lights.append(light);
    currentLightIndex_ = scene_.lights.size() - 1;
    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::TransformsOnly);
    renderWidget_->setSelection(selectedObjectIndices_, currentObjectIndex_);
    refreshLightsPanel();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentObjectIndex_,
        QStringLiteral("Added %1").arg(lightDisplayName(light, currentLightIndex_)),
        RenderWidget::SceneUpdateMode::TransformsOnly);
    markSceneDirty(QStringLiteral("Added %1").arg(lightDisplayName(light, currentLightIndex_)));
}

void MainWindow::importModelObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Import Model"),
        QFileInfo(scenePath_).absolutePath(),
        QStringLiteral("Model Files (*.obj *.gltf *.glb)"));
    if (files.isEmpty()) {
        return;
    }

    const SceneConfig before = scene_;
    const int beforeSelection = currentObjectIndex_;
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
    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::ReloadResources);
    refreshMaterialList();
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentObjectIndex_,
        QStringLiteral("Imported %1 model(s)").arg(insertedIndices.size()),
        RenderWidget::SceneUpdateMode::ReloadResources);
    markSceneDirty(QStringLiteral("Imported %1 model(s)").arg(insertedIndices.size()));
}

void MainWindow::duplicateSelectedObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();

    const QVector<int> selection = selectedObjectIndices_.isEmpty() && currentObjectIndex_ >= 0
        ? QVector<int>{currentObjectIndex_}
        : selectedObjectIndices_;
    const QVector<int> roots = topLevelSelectedIndices(scene_, selection);
    if (roots.isEmpty()) {
        return;
    }

    const SceneConfig before = scene_;
    const int beforeSelection = currentObjectIndex_;
    const QVector<RenderObjectConfig> prototypes = collectClipboardObjects(scene_, roots, false);
    const QVector<int> insertedIndices = appendClonedObjects(&scene_, prototypes, true, kDuplicateOffset);

    currentObjectIndex_ = insertedIndices.isEmpty() ? -1 : insertedIndices.constLast();
    selectedObjectIndices_ = insertedIndices;
    const RenderWidget::SceneUpdateMode updateMode =
        selectionIncludesModel(scene_, insertedIndices)
            ? RenderWidget::SceneUpdateMode::ReloadResources
            : RenderWidget::SceneUpdateMode::TransformsOnly;
    renderWidget_->setScene(scene_, updateMode);
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentObjectIndex_,
        QStringLiteral("Duplicated %1 object(s)").arg(roots.size()),
        updateMode);
    markSceneDirty(QStringLiteral("Duplicated %1 object(s)").arg(roots.size()));
}

void MainWindow::copySelectedObjects() {
    const QVector<int> selection = selectedObjectIndices_.isEmpty() && currentObjectIndex_ >= 0
        ? QVector<int>{currentObjectIndex_}
        : selectedObjectIndices_;
    const QVector<int> roots = topLevelSelectedIndices(scene_, selection);
    clipboardObjects_ = collectClipboardObjects(scene_, roots, true);
    statusBar()->showMessage(
        clipboardObjects_.isEmpty()
            ? QStringLiteral("Nothing copied")
            : QStringLiteral("Copied %1 object(s)").arg(roots.size()),
        1500);
}

void MainWindow::pasteObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();
    if (clipboardObjects_.isEmpty()) {
        return;
    }

    const SceneConfig before = scene_;
    const int beforeSelection = currentObjectIndex_;
    const QVector<int> insertedIndices = appendClonedObjects(&scene_, clipboardObjects_, false, kDuplicateOffset);
    if (insertedIndices.isEmpty()) {
        return;
    }

    currentObjectIndex_ = insertedIndices.constLast();
    selectedObjectIndices_ = insertedIndices;
    const RenderWidget::SceneUpdateMode updateMode =
        selectionIncludesModel(scene_, insertedIndices)
            ? RenderWidget::SceneUpdateMode::ReloadResources
            : RenderWidget::SceneUpdateMode::TransformsOnly;
    renderWidget_->setScene(scene_, updateMode);
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentObjectIndex_,
        QStringLiteral("Pasted %1 object(s)").arg(insertedIndices.size()),
        updateMode);
    markSceneDirty(QStringLiteral("Pasted %1 object(s)").arg(insertedIndices.size()));
}

void MainWindow::deleteSelectedObjects() {
    commitInspectorTransformEdits();
    commitLightEdits();
    const QVector<int> selection = selectedObjectIndices_.isEmpty() && currentObjectIndex_ >= 0
        ? QVector<int>{currentObjectIndex_}
        : selectedObjectIndices_;
    const QVector<int> roots = topLevelSelectedIndices(scene_, selection);
    if (roots.isEmpty()) {
        return;
    }

    const QVector<int> removalIndices = collectSubtreeSelection(scene_, roots);
    const SceneConfig before = scene_;
    const int beforeSelection = currentObjectIndex_;
    QVector<int> descendingRemoval = removalIndices;
    std::sort(descendingRemoval.begin(), descendingRemoval.end(), std::greater<int>());
    for (int index : descendingRemoval) {
        scene_.objects.removeAt(index);
    }

    currentObjectIndex_ = clampSelectedIndex(scene_, currentObjectIndex_);
    selectedObjectIndices_ = currentObjectIndex_ >= 0 ? QVector<int>{currentObjectIndex_} : QVector<int>{};
    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::TransformsOnly);
    refreshObjectTree();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentObjectIndex_,
        QStringLiteral("Deleted %1 object(s)").arg(removalIndices.size()),
        RenderWidget::SceneUpdateMode::TransformsOnly);
    markSceneDirty(QStringLiteral("Deleted %1 object(s)").arg(removalIndices.size()));
}

void MainWindow::removeSelectedLight() {
    commitInspectorTransformEdits();
    commitLightEdits();
    if (currentLightIndex_ < 0 || currentLightIndex_ >= scene_.lights.size() || scene_.lights.size() <= 1) {
        return;
    }

    const SceneConfig before = scene_;
    const int beforeSelection = currentObjectIndex_;
    const QString description =
        QStringLiteral("Removed %1").arg(lightDisplayName(scene_.lights.at(currentLightIndex_), currentLightIndex_));

    scene_.lights.removeAt(currentLightIndex_);
    currentLightIndex_ = qBound(0, currentLightIndex_, scene_.lights.size() - 1);
    renderWidget_->setScene(scene_, RenderWidget::SceneUpdateMode::TransformsOnly);
    renderWidget_->setSelection(selectedObjectIndices_, currentObjectIndex_);
    refreshLightsPanel();

    pushSceneCommand(
        before,
        scene_,
        beforeSelection,
        currentObjectIndex_,
        description,
        RenderWidget::SceneUpdateMode::TransformsOnly);
    markSceneDirty(description);
}

bool MainWindow::saveScene() {
    commitInspectorTransformEdits();
    commitLightEdits();
    scenegraph::ensureObjectIds(&scene_);

    try {
        SceneConfig::saveToFile(scene_, scenePath_);
        undoStack_->setClean();
        dirty_ = false;
        updateWindowCaption();
        statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(scenePath_).fileName()), 3000);
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
        scene_ = SceneConfig::loadFromFile(scenePath_);
        scenegraph::ensureObjectIds(&scene_);
        currentObjectIndex_ = clampSelectedIndex(scene_, 0);
        applySceneState(scene_, currentObjectIndex_, RenderWidget::SceneUpdateMode::ReloadResources, true);
        undoStack_->clear();
        undoStack_->setClean();
        dirty_ = false;
        updateWindowCaption();
        statusBar()->showMessage(QStringLiteral("Reloaded scene"), 3000);
    } catch (const std::exception& exception) {
        QMessageBox::critical(this, QStringLiteral("Reload Error"), QString::fromUtf8(exception.what()));
    }
}

bool MainWindow::maybeSaveChanges(const QString& actionText) {
    if (!dirty_ && !sceneEditSessionActive_) {
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
    const QString dirtyMarker = dirty_ ? QStringLiteral(" *") : QString();
    const QString fileName = QFileInfo(scenePath_).fileName();
    setWindowTitle(QStringLiteral("%1%2 - %3").arg(scene_.window.title, dirtyMarker, fileName));
}

void MainWindow::markSceneDirty(const QString& reason) {
    dirty_ = true;
    updateWindowCaption();
    statusBar()->showMessage(reason, 1500);
}

}  // namespace renderer
