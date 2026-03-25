#pragma once

#include <QHash>
#include <QMatrix4x4>
#include <QString>
#include <QVector>
#include <QVector3D>

#include "app/SceneConfig.hpp"

namespace renderer::scenegraph {

QString generateObjectId();
QString uniqueObjectName(const SceneConfig& scene, const QString& baseName);
void ensureObjectIds(SceneConfig* scene);
QHash<QString, int> buildIdLookup(const SceneConfig& scene);
QVector<int> childIndices(const SceneConfig& scene, const QString& parentId);
QVector<int> rootIndices(const SceneConfig& scene);
QVector<int> collectSubtree(const SceneConfig& scene, int rootIndex);
QMatrix4x4 buildLocalTransform(const RenderObjectConfig& object);
QMatrix4x4 buildWorldTransform(const SceneConfig& scene, int index);
QVector3D worldPosition(const SceneConfig& scene, int index);

}  // namespace renderer::scenegraph
