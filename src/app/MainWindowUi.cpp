#include "app/MainWindow.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QUndoStack>
namespace {

constexpr int kLayoutVersion = 1;
const char kSettingsOrganization[] = "OpenGLStudy";
const char kSettingsApplication[] = "QtRendererEditor";

QDoubleSpinBox* createSpinBox(double minimum, double maximum, double step) {
    auto* spinBox = new QDoubleSpinBox;
    spinBox->setRange(minimum, maximum);
    spinBox->setSingleStep(step);
    spinBox->setDecimals(3);
    spinBox->setAccelerated(true);
    return spinBox;
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

}  // namespace

namespace renderer {

void MainWindow::createSceneResourceActions() {
    addCubeAction_ = new QAction(QStringLiteral("Add Cube"), this);
    addCubeAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    addCubeAction_->setStatusTip(QStringLiteral("Create a cube object"));
    connect(addCubeAction_, &QAction::triggered, this, &MainWindow::addCubeObject);

    importModelAction_ = new QAction(QStringLiteral("Import Model"), this);
    importModelAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    importModelAction_->setStatusTip(QStringLiteral("Import model files into the scene"));
    connect(importModelAction_, &QAction::triggered, this, &MainWindow::importModelObjects);

    addLightAction_ = new QAction(QStringLiteral("Add Light"), this);
    addLightAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    addLightAction_->setStatusTip(QStringLiteral("Create a light in the scene"));
    connect(addLightAction_, &QAction::triggered, this, &MainWindow::addLight);
}

void MainWindow::populateAddResourceMenu(QMenu* menu) {
    if (!menu) {
        return;
    }

    menu->clear();
    menu->addAction(addCubeAction_);
    menu->addAction(importModelAction_);
    menu->addAction(addLightAction_);
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    auto* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    auto* addMenu = menuBar()->addMenu(QStringLiteral("Add"));
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

    populateAddResourceMenu(addMenu);

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
    toolbar->addSeparator();
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);
    toolbar->addAction(copyAction);
    toolbar->addAction(pasteAction);
    toolbar->addAction(duplicateAction);
    toolbar->addSeparator();

    auto* toolbarAddButton = new QToolButton(toolbar);
    toolbarAddButton->setText(QStringLiteral("Add"));
    toolbarAddButton->setPopupMode(QToolButton::InstantPopup);
    auto* toolbarAddMenu = new QMenu(toolbarAddButton);
    populateAddResourceMenu(toolbarAddMenu);
    toolbarAddButton->setMenu(toolbarAddMenu);
    toolbar->addWidget(toolbarAddButton);

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

    auto* sceneWidget = new QWidget(sceneDock_);
    auto* sceneLayout = new QVBoxLayout(sceneWidget);
    sceneLayout->setContentsMargins(8, 8, 8, 8);
    sceneLayout->setSpacing(8);

    auto* sceneToolbarRow = new QWidget(sceneWidget);
    auto* sceneToolbarLayout = new QHBoxLayout(sceneToolbarRow);
    sceneToolbarLayout->setContentsMargins(0, 0, 0, 0);
    sceneToolbarLayout->setSpacing(8);

    auto* addButton = new QToolButton(sceneToolbarRow);
    addButton->setText(QStringLiteral("Add"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(addButton);
    populateAddResourceMenu(addMenu);
    addButton->setMenu(addMenu);

    auto* resourceLabel = new QLabel(QStringLiteral("Objects, models and lights"), sceneToolbarRow);
    sceneToolbarLayout->addWidget(addButton);
    sceneToolbarLayout->addWidget(resourceLabel);
    sceneToolbarLayout->addStretch(1);

    sceneLayout->addWidget(sceneToolbarRow);
    sceneLayout->addWidget(objectTree_, 1);
    sceneDock_->setWidget(sceneWidget);
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

    layout->addWidget(lightGroup);
    layout->addStretch(1);
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
        if (before.debug.snapEnabled != scene_.debug.snapEnabled) {
            pushSceneCommand(
                before,
                scene_,
                currentSelectionToken(),
                currentSelectionToken(),
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
        if (before.debug.gridStep != scene_.debug.gridStep) {
            pushSceneCommand(
                before,
                scene_,
                currentSelectionToken(),
                currentSelectionToken(),
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
        if (before.debug.rotateSnapDegrees != scene_.debug.rotateSnapDegrees) {
            pushSceneCommand(
                before,
                scene_,
                currentSelectionToken(),
                currentSelectionToken(),
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
        if (before.debug.scaleSnapStep != scene_.debug.scaleSnapStep) {
            pushSceneCommand(
                before,
                scene_,
                currentSelectionToken(),
                currentSelectionToken(),
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

}  // namespace renderer
