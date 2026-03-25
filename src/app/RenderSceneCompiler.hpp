#pragma once

#include <functional>

#include "app/RenderBackendTypes.hpp"
#include "app/RenderResourceManager.hpp"

namespace renderer {

class RenderSceneCompiler final {
public:
    using PathResolver = RenderResourceManager::PathResolver;

    static CompiledRenderScene compile(
        const SceneConfig& scene,
        const RenderResourceManager& resources,
        const PathResolver& pathResolver);
};

}  // namespace renderer
