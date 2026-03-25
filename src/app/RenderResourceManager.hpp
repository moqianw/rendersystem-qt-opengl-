#pragma once

#include <QHash>
#include <QString>

#include <functional>
#include <memory>

#include "app/RenderBackendTypes.hpp"

namespace renderer {

class RenderResourceManager final {
public:
    using PathResolver = std::function<QString(const QString&)>;

    void sync(const SceneConfig& scene, bool reloadResources, const PathResolver& pathResolver);
    void clear();

    MaterialResourcePtr material(const QString& materialId) const;
    std::shared_ptr<const ModelResource> model(const QString& sourcePath) const;

private:
    void rebuildMaterials(const SceneConfig& scene, const PathResolver& pathResolver);
    void rebuildModels(const SceneConfig& scene, const PathResolver& pathResolver);
    static QString resolvedModelKey(const QString& sourcePath, const PathResolver& pathResolver);

    QHash<QString, MaterialResourcePtr> materials_;
    QHash<QString, std::shared_ptr<ModelResource>> models_;
};

}  // namespace renderer
