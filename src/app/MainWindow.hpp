#pragma once

#include <QMainWindow>
#include <QVector>

#include "app/RenderWidget.hpp"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QUndoStack;

namespace renderer {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const SceneConfig& scene, const QString& scenePath, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenus();
    void createDocks();
    void createLightsPanel(QDockWidget* dock);
    void createCameraPanel(QDockWidget* dock);
    void createToolsPanel(QDockWidget* dock);
    void restoreDefaultLayout();
    void showAllPanels();
    bool restoreEditorLayout();
    void saveEditorLayout() const;
    void refreshObjectTree();
    void refreshMaterialList();
    void refreshLightsPanel();
    void refreshLightInspector();
    void refreshToolsPanel();
    void setSelectionState(const QVector<int>& indices, int currentIndex);
    void handleObjectTreeSelectionChanged();
    void handleLightSelectionChanged();
    void refreshInspector();
    void previewLightEdits();
    void commitLightEdits();
    void previewInspectorTransformEdits();
    void commitInspectorTransformEdits();
    void applyInspectorMetadataEdits();
    void beginSceneEditSession(const QString& description, RenderWidget::SceneUpdateMode updateMode);
    void commitSceneEditSession();
    void applySceneState(
        const SceneConfig& scene,
        int selectedObjectIndex,
        RenderWidget::SceneUpdateMode updateMode,
        bool resetCamera = false);
    void pushSceneCommand(
        const SceneConfig& before,
        const SceneConfig& after,
        int beforeSelection,
        int afterSelection,
        const QString& description,
        RenderWidget::SceneUpdateMode updateMode);
    void applyViewportTransformPreview(
        int index,
        const QVector3D& position,
        const QVector3D& rotationDegrees,
        const QVector3D& scale);
    void commitViewportTransform(
        int index,
        const QVector3D& position,
        const QVector3D& rotationDegrees,
        const QVector3D& scale,
        RenderWidget::TransformMode mode);
    void updateCameraPanel(const QVector3D& position, const QVector3D& target, float distance);
    void setTransformMode(RenderWidget::TransformMode mode);
    void setCoordinateSpace(RenderWidget::CoordinateSpace space);
    void addCubeObject();
    void addLight();
    void importModelObjects();
    void duplicateSelectedObjects();
    void copySelectedObjects();
    void pasteObjects();
    void deleteSelectedObjects();
    void removeSelectedLight();
    bool saveScene();
    void reloadScene();
    bool maybeSaveChanges(const QString& actionText);
    void updateWindowCaption();
    void markSceneDirty(const QString& reason);

    QString scenePath_;
    SceneConfig scene_;
    SceneConfig sceneEditBefore_;
    RenderWidget* renderWidget_ = nullptr;
    QUndoStack* undoStack_ = nullptr;
    QDockWidget* sceneDock_ = nullptr;
    QDockWidget* inspectorDock_ = nullptr;
    QDockWidget* materialDock_ = nullptr;
    QDockWidget* lightsDock_ = nullptr;
    QDockWidget* cameraDock_ = nullptr;
    QDockWidget* toolsDock_ = nullptr;
    QTreeWidget* objectTree_ = nullptr;
    QListWidget* materialList_ = nullptr;
    QListWidget* lightList_ = nullptr;
    QPushButton* addLightButton_ = nullptr;
    QPushButton* removeLightButton_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QComboBox* materialCombo_ = nullptr;
    QComboBox* lightTypeCombo_ = nullptr;
    QCheckBox* visibleCheck_ = nullptr;
    QCheckBox* snapEnabledCheck_ = nullptr;
    QComboBox* transformModeCombo_ = nullptr;
    QComboBox* coordinateSpaceCombo_ = nullptr;
    QLabel* cameraPositionLabel_ = nullptr;
    QLabel* cameraTargetLabel_ = nullptr;
    QLabel* cameraDistanceLabel_ = nullptr;
    QDoubleSpinBox* moveSnapStepEdit_ = nullptr;
    QDoubleSpinBox* rotateSnapStepEdit_ = nullptr;
    QDoubleSpinBox* scaleSnapStepEdit_ = nullptr;
    QDoubleSpinBox* lightPositionEdits_[3]{};
    QDoubleSpinBox* lightDirectionEdits_[3]{};
    QDoubleSpinBox* lightColorEdits_[3]{};
    QDoubleSpinBox* lightAmbientEdit_ = nullptr;
    QDoubleSpinBox* lightIntensityEdit_ = nullptr;
    QDoubleSpinBox* lightRangeEdit_ = nullptr;
    QDoubleSpinBox* lightInnerConeEdit_ = nullptr;
    QDoubleSpinBox* lightOuterConeEdit_ = nullptr;
    QDoubleSpinBox* positionEdits_[3]{};
    QDoubleSpinBox* rotationEdits_[3]{};
    QDoubleSpinBox* scaleEdits_[3]{};
    bool syncingInspector_ = false;
    bool syncingLights_ = false;
    bool applyingSceneState_ = false;
    bool sceneEditSessionActive_ = false;
    bool dirty_ = false;
    int currentObjectIndex_ = -1;
    int currentLightIndex_ = -1;
    QVector<int> selectedObjectIndices_;
    QVector<RenderObjectConfig> clipboardObjects_;
    int sceneEditBeforeSelection_ = -1;
    RenderWidget::SceneUpdateMode sceneEditUpdateMode_ =
        RenderWidget::SceneUpdateMode::TransformsOnly;
    QString sceneEditDescription_;
};

}  // namespace renderer
