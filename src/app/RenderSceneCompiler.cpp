#include "app/RenderSceneCompiler.hpp"

#include "app/SceneGraph.hpp"

namespace {

const QVector3D kDefaultBoundsMin(-0.5f, -0.5f, -0.5f);
const QVector3D kDefaultBoundsMax(0.5f, 0.5f, 0.5f);

renderer::MaterialResourcePtr resolveObjectMaterial(
    const renderer::RenderObjectConfig& object,
    int materialSlot,
    const renderer::RenderResourceManager& resources) {
    if (!object.materialId.isEmpty()) {
        return resources.material(object.materialId);
    }
    if (materialSlot >= 0 && materialSlot < object.materialIds.size()) {
        return resources.material(object.materialIds.at(materialSlot));
    }
    return resources.material(QString());
}

}  // namespace

namespace renderer {

CompiledRenderScene RenderSceneCompiler::compile(
    const SceneConfig& scene,
    const RenderResourceManager& resources,
    const PathResolver& pathResolver) {
    CompiledRenderScene compiled;
    compiled.lights = scene.lights;
    compiled.objects.resize(scene.objects.size());

    for (int index = 0; index < scene.objects.size(); ++index) {
        const RenderObjectConfig& object = scene.objects.at(index);

        RenderObjectInstance instance;
        instance.objectIndex = index;
        instance.visible = object.visible;
        instance.worldTransform = scenegraph::buildWorldTransform(scene, index);

        if (object.geometry == GeometryType::Model && !object.sourcePath.isEmpty() && pathResolver) {
            const auto model = resources.model(pathResolver(object.sourcePath));
            if (model) {
                instance.localBoundsMin = model->boundsMin;
                instance.localBoundsMax = model->boundsMax;
                compiled.objects[index] = instance;

                for (const auto& part : model->parts) {
                    if (!part || !part->valid || part->mesh.indexCount <= 0) {
                        continue;
                    }

                    RenderItem item;
                    item.objectIndex = index;
                    item.visible = object.visible;
                    item.geometrySource = RenderGeometrySource::ModelPart;
                    item.mesh = &part->mesh;
                    item.material = resolveObjectMaterial(object, part->materialSlot, resources);
                    item.modelMatrix = instance.worldTransform;
                    item.localBoundsMin = model->boundsMin;
                    item.localBoundsMax = model->boundsMax;
                    compiled.items.append(item);
                }
                continue;
            }

            instance.localBoundsMin = kDefaultBoundsMin;
            instance.localBoundsMax = kDefaultBoundsMax;
            compiled.objects[index] = instance;
            continue;
        }

        instance.localBoundsMin = kDefaultBoundsMin;
        instance.localBoundsMax = kDefaultBoundsMax;
        compiled.objects[index] = instance;

        RenderItem item;
        item.objectIndex = index;
        item.visible = object.visible;
        item.geometrySource = RenderGeometrySource::Cube;
        item.material = resources.material(object.materialId);
        item.modelMatrix = instance.worldTransform;
        item.localBoundsMin = instance.localBoundsMin;
        item.localBoundsMax = instance.localBoundsMax;
        compiled.items.append(item);
    }

    return compiled;
}

}  // namespace renderer
