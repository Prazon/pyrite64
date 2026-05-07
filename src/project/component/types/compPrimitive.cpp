/**
* Editor-side Primitive component (added by SPBF64 fork).
* Pairs with engine: n64/engine/src/scene/components/primitive.cpp
*
* Lets authors place arbitrary 3D shapes (Box, Sphere, Cylinder, Capsule,
* Cone, Pyramid, Plane) directly in the scene without authoring a glb.
* Editor preview renders as a wireframe in the chosen color (matching the
* CollBody gizmo style); the N64 runtime renders a lit solid mesh generated
* procedurally at component init.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/colors.h"
#include "../../../utils/meshGen.h"
#include "../../../editor/pages/parts/viewport3D.h"

#include "imgui.h"
#include <algorithm>

namespace
{
  // Keep these in lock-step with engine's P64::Comp::Primitive::ShapeType.
  constexpr int32_t TYPE_BOX      = 0;
  constexpr int32_t TYPE_SPHERE   = 1;
  constexpr int32_t TYPE_CYLINDER = 2;
  constexpr int32_t TYPE_CAPSULE  = 3;
  constexpr int32_t TYPE_CONE     = 4;
  constexpr int32_t TYPE_PYRAMID  = 5;
  constexpr int32_t TYPE_PLANE    = 6;
}

namespace Project::Component::Primitive
{
  struct Data
  {
    PROP_S32(type);
    PROP_VEC3(halfExtend);
    PROP_VEC4(color);
    PROP_S32(layerIdx);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->type.value = TYPE_BOX;
    data->halfExtend.value = {16.0f, 16.0f, 16.0f};
    data->color.value = {0.7f, 0.7f, 0.7f, 1.0f};
    data->layerIdx.value = 0;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    return Utils::JSON::Builder{}
      .set(data.type)
      .set(data.halfExtend)
      .set(data.color)
      .set(data.layerIdx)
      .doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->type, TYPE_BOX);
    Utils::JSON::readProp(doc, data->halfExtend, glm::vec3{16.0f, 16.0f, 16.0f});
    Utils::JSON::readProp(doc, data->color, glm::vec4{0.7f, 0.7f, 0.7f, 1.0f});
    Utils::JSON::readProp(doc, data->layerIdx, 0);
    return data;
  }

  // Binary layout MUST match engine's InitData in primitive.cpp.
  void build(Object& obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    auto type = data.type.resolve(obj.propOverrides);
    auto ext  = data.halfExtend.resolve(obj.propOverrides);
    auto col  = data.color.resolve(obj.propOverrides);

    ctx.fileObj.write<uint8_t>((uint8_t)type);
    ctx.fileObj.write<uint8_t>((uint8_t)data.layerIdx.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.r * 255.0f), 0, 255));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.g * 255.0f), 0, 255));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.b * 255.0f), 0, 255));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.a * 255.0f), 0, 255));
    ctx.fileObj.align(4);
    ctx.fileObj.write<float>(ext.x);
    ctx.fileObj.write<float>(ext.y);
    ctx.fileObj.write<float>(ext.z);
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      ImTable::addComboBox("Shape", data.type.value,
        {"Box", "Sphere", "Cylinder", "Capsule", "Cone", "Pyramid", "Plane"});

      auto &ext = data.halfExtend.resolve(obj.propOverrides);
      auto type = data.type.resolve(obj.propOverrides);

      switch(type) {
        case TYPE_BOX:
          ImTable::addObjProp("Half Size", data.halfExtend);
          break;
        case TYPE_SPHERE:
          ImTable::add("Radius", ext.y);
          ext.x = ext.y;
          ext.z = ext.y;
          break;
        case TYPE_CYLINDER:
        case TYPE_CONE:
          ImTable::add("Radius", ext.x);
          ImTable::add("Half Height", ext.y);
          ext.z = ext.x;
          break;
        case TYPE_CAPSULE:
          ImTable::add("Radius", ext.x);
          ImTable::add("Inner Half Height", ext.y);
          ext.z = ext.x;
          break;
        case TYPE_PYRAMID:
          ImTable::add("Base X Half", ext.x);
          ImTable::add("Half Height", ext.y);
          ImTable::add("Base Z Half", ext.z);
          break;
        case TYPE_PLANE:
          ImTable::add("Half X", ext.x);
          ImTable::add("Half Z", ext.z);
          ext.y = 0.0f;
          break;
      }

      ImTable::addObjProp("Color", data.color);

      auto scene = ctx.project->getScenes().getLoadedScene();
      if (scene) {
        std::vector<const char*> layerNames{};
        for (auto &layer : scene->conf.layers3D) {
          layerNames.push_back(layer.name.value.c_str());
        }
        ImTable::addObjProp<int32_t>("Draw-Layer", data.layerIdx,
          [&layerNames](int32_t *layer) {
            return ImGui::Combo("##", layer, layerNames.data(), (int)layerNames.size());
          }, nullptr);
      }

      ImTable::end();

      // Phase 4 polish: one-click matching CollBody. Convenient when authoring
      // platforms / blockers so visual + collider stay in sync.
      if (ImGui::Button("Add Matching CollBody")) {
        size_t before = obj.components.size();
        obj.addComponent(5 /* CollBody id */);
        if (obj.components.size() > before) {
          auto &newEntry = obj.components.back();
          auto &collDef = TABLE[5];
          nlohmann::json doc = collDef.funcSerialize(newEntry);
          doc["type"] = (int)data.type.resolve(obj.propOverrides);
          glm::vec3 he = data.halfExtend.resolve(obj.propOverrides);
          doc["halfExtend"] = nlohmann::json::array({he.x, he.y, he.z});
          newEntry.data = collDef.funcDeserialize(doc);
        }
      }
    }
  }

  void draw3D(Object& obj, Entry &entry, Editor::Viewport3D &vp,
              SDL_GPUCommandBuffer* /*cmdBuff*/, SDL_GPURenderPass* /*pass*/)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    auto &objPos = obj.pos.resolve(obj.propOverrides);
    auto &objRot = obj.rot.resolve(obj.propOverrides);
    auto &objScale = obj.scale.resolve(obj.propOverrides);

    glm::vec3 halfExt = data.halfExtend.resolve(obj.propOverrides) * objScale;
    auto col = data.color.resolve(obj.propOverrides);
    glm::u8vec4 color{
      (uint8_t)std::clamp((int)(col.r * 255.0f), 0, 255),
      (uint8_t)std::clamp((int)(col.g * 255.0f), 0, 255),
      (uint8_t)std::clamp((int)(col.b * 255.0f), 0, 255),
      (uint8_t)std::clamp((int)(col.a * 255.0f), 0, 255)
    };
    if (ctx.isObjectSelected(obj.uuid)) {
      color = Utils::Colors::kSelectionTint;
    }

    auto type = data.type.resolve(obj.propOverrides);
    switch(type) {
      case TYPE_BOX:
      case TYPE_PLANE:
        Utils::Mesh::addLineBox(*vp.getLines(), objPos, halfExt, color, objRot);
        break;
      case TYPE_SPHERE:
        Utils::Mesh::addLineSphere(*vp.getLines(), objPos, halfExt, color, objRot);
        break;
      case TYPE_CYLINDER:
        Utils::Mesh::addLineCylinder(*vp.getLines(), objPos, halfExt, color, objRot);
        break;
      case TYPE_CAPSULE:
        Utils::Mesh::addLineCapsule(*vp.getLines(), objPos, halfExt, color, objRot);
        break;
      case TYPE_CONE:
        Utils::Mesh::addLineCone(*vp.getLines(), objPos, halfExt, color, objRot);
        break;
      case TYPE_PYRAMID:
        Utils::Mesh::addLinePyramid(*vp.getLines(), objPos, halfExt, color, objRot);
        break;
    }
  }
}
