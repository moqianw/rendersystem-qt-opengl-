#include "app/RenderSceneBackend.hpp"

#include "app/RenderMath.hpp"
#include "app/RenderSceneCompiler.hpp"

namespace {

constexpr float kDefaultBoundsExtent = 0.5f;

QVector3D defaultMinBounds() {
    return QVector3D(-kDefaultBoundsExtent, -kDefaultBoundsExtent, -kDefaultBoundsExtent);
}

QVector3D defaultMaxBounds() {
    return QVector3D(kDefaultBoundsExtent, kDefaultBoundsExtent, kDefaultBoundsExtent);
}

}  // namespace

namespace renderer {

void RenderSceneBackend::setPathResolver(PathResolver pathResolver) {
    pathResolver_ = std::move(pathResolver);
}

void RenderSceneBackend::sync(const SceneConfig& scene, bool reloadResources) {
    if (!pathResolver_) {
        compiledScene_.clear();
        return;
    }

    resourceManager_.sync(scene, reloadResources, pathResolver_);
    compiledScene_ = RenderSceneCompiler::compile(scene, resourceManager_, pathResolver_);
}

void RenderSceneBackend::clear() {
    compiledScene_.clear();
    resourceManager_.clear();
}

const CompiledRenderScene& RenderSceneBackend::compiledScene() const {
    return compiledScene_;
}

QMatrix4x4 RenderSceneBackend::objectWorldTransform(int index) const {
    if (index < 0 || index >= compiledScene_.objects.size()) {
        return QMatrix4x4();
    }
    return rendermath::toQt(compiledScene_.objects.at(index).worldTransform);
}

QVector3D RenderSceneBackend::objectLocalBoundsMin(int index) const {
    if (index < 0 || index >= compiledScene_.objects.size()) {
        return defaultMinBounds();
    }
    return rendermath::toQt(compiledScene_.objects.at(index).localBoundsMin);
}

QVector3D RenderSceneBackend::objectLocalBoundsMax(int index) const {
    if (index < 0 || index >= compiledScene_.objects.size()) {
        return defaultMaxBounds();
    }
    return rendermath::toQt(compiledScene_.objects.at(index).localBoundsMax);
}

}  // namespace renderer
