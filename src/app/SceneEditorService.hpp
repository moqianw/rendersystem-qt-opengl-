#pragma once

#include <QVector>
#include <QVector3D>

#include "app/SceneConfig.hpp"

namespace renderer {

enum class SceneUpdateImpact {
    TransformsOnly,
    ReloadResources
};

struct SceneSelectionState {
    QVector<int> selectedObjectIndices;
    int currentObjectIndex = -1;
    int currentLightIndex = -1;
};

struct SceneCopyResult {
    QVector<RenderObjectConfig> clipboardObjects;
    int rootCount = 0;
};

struct SceneEditResult {
    bool changed = false;
    SceneSelectionState selection;
    SceneUpdateImpact updateImpact = SceneUpdateImpact::TransformsOnly;
    QString description;
};

class SceneEditorService final {
public:
    static SceneCopyResult copySelection(
        const SceneConfig& scene,
        const SceneSelectionState& selectionState);

    static SceneEditResult addCube(
        SceneConfig* scene,
        const SceneSelectionState& selectionState);

    static SceneEditResult addLight(
        SceneConfig* scene,
        const SceneSelectionState& selectionState);

    static SceneEditResult duplicateSelection(
        SceneConfig* scene,
        const SceneSelectionState& selectionState,
        const QVector3D& rootOffset);

    static SceneEditResult pasteClipboard(
        SceneConfig* scene,
        const QVector<RenderObjectConfig>& clipboardObjects,
        const QVector3D& rootOffset);

    static SceneEditResult deleteSelection(
        SceneConfig* scene,
        const SceneSelectionState& selectionState);

    static SceneEditResult removeSelectedLight(
        SceneConfig* scene,
        const SceneSelectionState& selectionState);
};

}  // namespace renderer
