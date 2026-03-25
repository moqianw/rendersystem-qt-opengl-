#include "app/SceneEditorService.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <functional>

#include "app/SceneGraph.hpp"

namespace {

constexpr float kMinimumLightRange = 0.1f;
const QVector3D kDefaultLightDirection(-0.45f, -1.0f, -0.3f);

int clampSelectedIndex(const renderer::SceneConfig& scene, int index) {
    if (scene.objects.isEmpty()) {
        return -1;
    }

    return qBound(0, index < 0 ? 0 : index, scene.objects.size() - 1);
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

bool selectionIncludesModel(const renderer::SceneConfig& scene, const QVector<int>& selection) {
    for (int index : selection) {
        if (index >= 0 && index < scene.objects.size() && scene.objects.at(index).geometry == renderer::GeometryType::Model) {
            return true;
        }
    }
    return false;
}

QVector3D sanitizeLightDirection(const QVector3D& direction) {
    if (direction.lengthSquared() <= 1e-6f) {
        return kDefaultLightDirection;
    }
    return direction.normalized();
}

renderer::LightConfig sanitizeLight(renderer::LightConfig light) {
    light.direction = sanitizeLightDirection(light.direction);
    light.range = qMax(kMinimumLightRange, light.range);
    light.innerConeDegrees = qBound(0.1f, light.innerConeDegrees, 89.0f);
    light.outerConeDegrees = qBound(light.innerConeDegrees, light.outerConeDegrees, 89.5f);
    return light;
}

renderer::SceneSelectionState selectedObjectsOnly(const QVector<int>& insertedIndices) {
    renderer::SceneSelectionState selection;
    selection.selectedObjectIndices = insertedIndices;
    selection.currentObjectIndex = insertedIndices.isEmpty() ? -1 : insertedIndices.constLast();
    selection.currentLightIndex = -1;
    return selection;
}

renderer::SceneSelectionState normalizedObjectSelection(
    const renderer::SceneConfig& scene,
    const renderer::SceneSelectionState& selectionState) {
    renderer::SceneSelectionState selection = selectionState;
    selection.selectedObjectIndices = normalizeSelectionIndices(scene, selectionState.selectedObjectIndices);
    if (!selection.selectedObjectIndices.contains(selection.currentObjectIndex)) {
        selection.currentObjectIndex =
            selection.selectedObjectIndices.isEmpty() ? -1 : selection.selectedObjectIndices.constLast();
    }
    selection.currentLightIndex = -1;
    return selection;
}

QString lightDisplayName(const renderer::LightConfig& light, int index) {
    QString typeLabel = QStringLiteral("Light");
    switch (light.type) {
    case renderer::LightType::Directional:
        typeLabel = QStringLiteral("Directional");
        break;
    case renderer::LightType::Point:
        typeLabel = QStringLiteral("Point");
        break;
    case renderer::LightType::Spot:
        typeLabel = QStringLiteral("Spot");
        break;
    }

    return QStringLiteral("%1. %2 Light").arg(index + 1).arg(typeLabel);
}

}  // namespace

namespace renderer {

SceneCopyResult SceneEditorService::copySelection(
    const SceneConfig& scene,
    const SceneSelectionState& selectionState) {
    const QVector<int> selection = selectionState.selectedObjectIndices.isEmpty() && selectionState.currentObjectIndex >= 0
        ? QVector<int>{selectionState.currentObjectIndex}
        : selectionState.selectedObjectIndices;
    const QVector<int> roots = topLevelSelectedIndices(scene, selection);

    SceneCopyResult result;
    result.clipboardObjects = collectClipboardObjects(scene, roots, true);
    result.rootCount = roots.size();
    return result;
}

SceneEditResult SceneEditorService::addCube(
    SceneConfig* scene,
    const SceneSelectionState& selectionState) {
    SceneEditResult result;
    if (!scene) {
        return result;
    }

    RenderObjectConfig object;
    object.id = scenegraph::generateObjectId();
    object.name = scenegraph::uniqueObjectName(*scene, QStringLiteral("cube"));
    if (!scene->materials.isEmpty()) {
        object.materialId = scene->materials.constFirst().id;
    }
    if (selectionState.currentObjectIndex >= 0 && selectionState.currentObjectIndex < scene->objects.size()) {
        object.parentId = scene->objects.at(selectionState.currentObjectIndex).parentId;
        object.position = scene->objects.at(selectionState.currentObjectIndex).position + QVector3D(1.5f, 0.0f, 0.0f);
    }

    scene->objects.append(object);
    result.changed = true;
    result.selection = selectedObjectsOnly(QVector<int>{static_cast<int>(scene->objects.size() - 1)});
    result.description = QStringLiteral("Added %1").arg(object.name);
    return result;
}

SceneEditResult SceneEditorService::addLight(
    SceneConfig* scene,
    const SceneSelectionState& selectionState) {
    SceneEditResult result;
    if (!scene) {
        return result;
    }

    LightConfig light;
    if (selectionState.currentLightIndex >= 0 && selectionState.currentLightIndex < scene->lights.size()) {
        light = scene->lights.at(selectionState.currentLightIndex);
        light.position += QVector3D(1.5f, 1.0f, 0.0f);
    }
    light = sanitizeLight(light);

    scene->lights.append(light);
    result.changed = true;
    result.selection.currentLightIndex = scene->lights.size() - 1;
    result.description = QStringLiteral("Added %1").arg(lightDisplayName(light, result.selection.currentLightIndex));
    return result;
}

SceneEditResult SceneEditorService::duplicateSelection(
    SceneConfig* scene,
    const SceneSelectionState& selectionState,
    const QVector3D& rootOffset) {
    SceneEditResult result;
    if (!scene) {
        return result;
    }

    const QVector<int> selection = selectionState.selectedObjectIndices.isEmpty() && selectionState.currentObjectIndex >= 0
        ? QVector<int>{selectionState.currentObjectIndex}
        : selectionState.selectedObjectIndices;
    const QVector<int> roots = topLevelSelectedIndices(*scene, selection);
    if (roots.isEmpty()) {
        return result;
    }

    const QVector<RenderObjectConfig> prototypes = collectClipboardObjects(*scene, roots, false);
    const QVector<int> insertedIndices = appendClonedObjects(scene, prototypes, true, rootOffset);
    if (insertedIndices.isEmpty()) {
        return result;
    }

    result.changed = true;
    result.selection = selectedObjectsOnly(insertedIndices);
    result.updateImpact =
        selectionIncludesModel(*scene, insertedIndices) ? SceneUpdateImpact::ReloadResources : SceneUpdateImpact::TransformsOnly;
    result.description = QStringLiteral("Duplicated %1 object(s)").arg(roots.size());
    return result;
}

SceneEditResult SceneEditorService::pasteClipboard(
    SceneConfig* scene,
    const QVector<RenderObjectConfig>& clipboardObjects,
    const QVector3D& rootOffset) {
    SceneEditResult result;
    if (!scene || clipboardObjects.isEmpty()) {
        return result;
    }

    const QVector<int> insertedIndices = appendClonedObjects(scene, clipboardObjects, false, rootOffset);
    if (insertedIndices.isEmpty()) {
        return result;
    }

    result.changed = true;
    result.selection = selectedObjectsOnly(insertedIndices);
    result.updateImpact =
        selectionIncludesModel(*scene, insertedIndices) ? SceneUpdateImpact::ReloadResources : SceneUpdateImpact::TransformsOnly;
    result.description = QStringLiteral("Pasted %1 object(s)").arg(insertedIndices.size());
    return result;
}

SceneEditResult SceneEditorService::deleteSelection(
    SceneConfig* scene,
    const SceneSelectionState& selectionState) {
    SceneEditResult result;
    if (!scene) {
        return result;
    }

    const QVector<int> selection = selectionState.selectedObjectIndices.isEmpty() && selectionState.currentObjectIndex >= 0
        ? QVector<int>{selectionState.currentObjectIndex}
        : selectionState.selectedObjectIndices;
    const QVector<int> roots = topLevelSelectedIndices(*scene, selection);
    if (roots.isEmpty()) {
        return result;
    }

    const QVector<int> removalIndices = collectSubtreeSelection(*scene, roots);
    QVector<int> descendingRemoval = removalIndices;
    std::sort(descendingRemoval.begin(), descendingRemoval.end(), std::greater<int>());
    for (int index : descendingRemoval) {
        scene->objects.removeAt(index);
    }

    result.changed = true;
    result.selection.currentObjectIndex = clampSelectedIndex(*scene, selectionState.currentObjectIndex);
    if (result.selection.currentObjectIndex >= 0) {
        result.selection.selectedObjectIndices = {result.selection.currentObjectIndex};
    }
    result.description = QStringLiteral("Deleted %1 object(s)").arg(removalIndices.size());
    return result;
}

SceneEditResult SceneEditorService::removeSelectedLight(
    SceneConfig* scene,
    const SceneSelectionState& selectionState) {
    SceneEditResult result;
    if (!scene ||
        selectionState.currentLightIndex < 0 ||
        selectionState.currentLightIndex >= scene->lights.size() ||
        scene->lights.size() <= 1) {
        return result;
    }

    const QString description =
        QStringLiteral("Removed %1").arg(lightDisplayName(scene->lights.at(selectionState.currentLightIndex), selectionState.currentLightIndex));

    scene->lights.removeAt(selectionState.currentLightIndex);
    result.changed = true;
    result.selection.currentLightIndex = qBound(0, selectionState.currentLightIndex, scene->lights.size() - 1);
    result.description = description;
    return result;
}

}  // namespace renderer
