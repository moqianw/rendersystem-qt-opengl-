#pragma once

#include <QMatrix4x4>
#include <QVector3D>

#include "app/RenderResourceManager.hpp"

namespace renderer {

class RenderSceneBackend final {
public:
    using PathResolver = RenderResourceManager::PathResolver;

    void setPathResolver(PathResolver pathResolver);
    void sync(const SceneConfig& scene, bool reloadResources);
    void clear();

    const CompiledRenderScene& compiledScene() const;
    QMatrix4x4 objectWorldTransform(int index) const;
    QVector3D objectLocalBoundsMin(int index) const;
    QVector3D objectLocalBoundsMax(int index) const;

private:
    PathResolver pathResolver_;
    RenderResourceManager resourceManager_;
    CompiledRenderScene compiledScene_;
};

}  // namespace renderer
