#pragma once

#include <QObject>
#include <QString>
#include <QUndoStack>

#include <functional>

#include "app/RenderWidget.hpp"
#include "app/SceneConfig.hpp"

namespace renderer {

class SceneDocument final : public QObject {
    Q_OBJECT

public:
    using SceneUpdateMode = RenderWidget::SceneUpdateMode;
    using ApplySceneCallback =
        std::function<void(const SceneConfig& scene, int selectionToken, SceneUpdateMode updateMode, bool resetCamera)>;

    SceneDocument(SceneConfig* scene, const QString& scenePath, QObject* parent = nullptr);

    void setApplySceneCallback(ApplySceneCallback callback);

    QUndoStack* undoStack();
    const QString& scenePath() const;
    bool isDirty() const;
    bool hasUnsavedChanges() const;
    QString windowCaption() const;

    void clearHistory();
    void markDirty();
    void setClean();

    void beginEditSession(const QString& description, SceneUpdateMode updateMode, int selectionToken);
    void commitEditSession(int selectionToken);
    void pushSceneCommand(
        const SceneConfig& before,
        const SceneConfig& after,
        int beforeSelection,
        int afterSelection,
        const QString& description,
        SceneUpdateMode updateMode);
    void applySceneState(
        const SceneConfig& scene,
        int selectionToken,
        SceneUpdateMode updateMode,
        bool resetCamera = false);
    void save();
    void reload(int selectionToken);

signals:
    void dirtyChanged(bool dirty);

private:
    void updateDirtyState(bool dirty);

    SceneConfig* scene_ = nullptr;
    QString scenePath_;
    QUndoStack undoStack_;
    ApplySceneCallback applySceneCallback_;
    SceneConfig sceneEditBefore_;
    bool sceneEditSessionActive_ = false;
    bool dirty_ = false;
    int sceneEditBeforeSelection_ = -1;
    SceneUpdateMode sceneEditUpdateMode_ = SceneUpdateMode::TransformsOnly;
    QString sceneEditDescription_;
};

}  // namespace renderer
