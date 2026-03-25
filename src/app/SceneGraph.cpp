#include "app/SceneGraph.hpp"

#include <QSet>
#include <QUuid>

namespace renderer::scenegraph {

namespace {

void collectSubtreeRecursive(
    const SceneConfig& scene,
    int rootIndex,
    QSet<int>* visited,
    QVector<int>* subtree) {
    if (!visited || !subtree || rootIndex < 0 || rootIndex >= scene.objects.size() || visited->contains(rootIndex)) {
        return;
    }

    visited->insert(rootIndex);
    subtree->append(rootIndex);

    const QString rootId = scene.objects.at(rootIndex).id;
    for (int childIndex : childIndices(scene, rootId)) {
        collectSubtreeRecursive(scene, childIndex, visited, subtree);
    }
}

}  // namespace

QString generateObjectId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString uniqueObjectName(const SceneConfig& scene, const QString& baseName) {
    QString candidate = baseName.trimmed();
    if (candidate.isEmpty()) {
        candidate = QStringLiteral("object");
    }

    QSet<QString> names;
    for (const RenderObjectConfig& object : scene.objects) {
        names.insert(object.name);
    }

    if (!names.contains(candidate)) {
        return candidate;
    }

    int suffix = 2;
    while (names.contains(QStringLiteral("%1_%2").arg(candidate).arg(suffix))) {
        ++suffix;
    }

    return QStringLiteral("%1_%2").arg(candidate).arg(suffix);
}

void ensureObjectIds(SceneConfig* scene) {
    if (!scene) {
        return;
    }

    QSet<QString> usedIds;
    for (RenderObjectConfig& object : scene->objects) {
        if (object.id.isEmpty() || usedIds.contains(object.id)) {
            object.id = generateObjectId();
        }
        usedIds.insert(object.id);
    }

    const QHash<QString, int> idLookup = buildIdLookup(*scene);
    for (RenderObjectConfig& object : scene->objects) {
        if (!object.parentId.isEmpty() && !idLookup.contains(object.parentId)) {
            object.parentId.clear();
        }
    }

    for (RenderObjectConfig& object : scene->objects) {
        if (object.parentId.isEmpty()) {
            continue;
        }

        QSet<QString> visited;
        visited.insert(object.id);
        QString parentId = object.parentId;
        while (!parentId.isEmpty()) {
            if (visited.contains(parentId)) {
                object.parentId.clear();
                break;
            }

            visited.insert(parentId);
            const auto parentIt = idLookup.find(parentId);
            if (parentIt == idLookup.end()) {
                break;
            }

            parentId = scene->objects.at(parentIt.value()).parentId;
        }
    }
}

QHash<QString, int> buildIdLookup(const SceneConfig& scene) {
    QHash<QString, int> lookup;
    for (int index = 0; index < scene.objects.size(); ++index) {
        lookup.insert(scene.objects.at(index).id, index);
    }
    return lookup;
}

QVector<int> childIndices(const SceneConfig& scene, const QString& parentId) {
    QVector<int> children;
    for (int index = 0; index < scene.objects.size(); ++index) {
        if (scene.objects.at(index).parentId == parentId) {
            children.append(index);
        }
    }
    return children;
}

QVector<int> rootIndices(const SceneConfig& scene) {
    return childIndices(scene, QString());
}

QVector<int> collectSubtree(const SceneConfig& scene, int rootIndex) {
    QVector<int> subtree;
    QSet<int> visited;
    collectSubtreeRecursive(scene, rootIndex, &visited, &subtree);
    return subtree;
}

QMatrix4x4 buildLocalTransform(const RenderObjectConfig& object) {
    QMatrix4x4 model;
    model.translate(object.position);
    model.rotate(object.rotationDegrees.x(), 1.0f, 0.0f, 0.0f);
    model.rotate(object.rotationDegrees.y(), 0.0f, 1.0f, 0.0f);
    model.rotate(object.rotationDegrees.z(), 0.0f, 0.0f, 1.0f);
    model.scale(object.scale);
    return model;
}

QMatrix4x4 buildWorldTransform(const SceneConfig& scene, int index) {
    if (index < 0 || index >= scene.objects.size()) {
        return QMatrix4x4();
    }

    const QHash<QString, int> idLookup = buildIdLookup(scene);
    QMatrix4x4 world = buildLocalTransform(scene.objects.at(index));
    QString parentId = scene.objects.at(index).parentId;
    QSet<QString> visitedIds;
    visitedIds.insert(scene.objects.at(index).id);

    while (!parentId.isEmpty() && idLookup.contains(parentId) && !visitedIds.contains(parentId)) {
        visitedIds.insert(parentId);
        const int parentIndex = idLookup.value(parentId);
        world = buildLocalTransform(scene.objects.at(parentIndex)) * world;
        parentId = scene.objects.at(parentIndex).parentId;
    }

    return world;
}

QVector3D worldPosition(const SceneConfig& scene, int index) {
    return (buildWorldTransform(scene, index) * QVector4D(0.0f, 0.0f, 0.0f, 1.0f)).toVector3D();
}

}  // namespace renderer::scenegraph
