#pragma once

#include <vector>

#include "app/RenderBackendTypes.hpp"

namespace renderer::rendermesh {

void uploadMesh(
    MeshHandle* mesh,
    const std::vector<RenderVertex>& vertices,
    const std::vector<quint32>& indices,
    GLenum primitiveType);

void destroyMesh(MeshHandle* mesh);

}  // namespace renderer::rendermesh
