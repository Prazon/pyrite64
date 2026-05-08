/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "objectInspector.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "imgui_internal.h"
#include "../../imgui/helper.h"
#include "../../../context.h"
#include "../../../project/component/components.h"
#include "../../../project/scene/scene.h"
#include "../../../project/selection.h"
#include "../../selectionUtils.h"
#include "../../undoRedo.h"

Editor::ObjectInspector::ObjectInspector() {
}

void Editor::ObjectInspector::draw(Project::Scene &scene, Project::Selection &selection) {
  selection.sanitize(&scene);
  const auto &selectedIds = selection.all();
  if (selectedIds.empty()) {
    ImGui::Text("No Object selected");
    return;
  }

  if (selectedIds.size() > 1) {
    auto selectedObjects = Editor::SelectionUtils::collectSelectedObjects(scene, selection);

    if (selectedObjects.empty()) {
      selection.clear();
      ImGui::Text("No Object selected");
      return;
    }

    ImGui::Text("%zu Objects selected", selectedObjects.size());

    auto handleHistory = [&](const std::string &desc) {
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        Editor::UndoRedo::getHistory().markChanged(desc);
      }
    };

    auto floatEqual = [](float a, float b) {
      return std::abs(a - b) <= 0.0001f;
    };

    static std::unordered_map<std::string, std::string> mixedValueCache{};

    auto parseFloatList = [](const std::string &text, float *out, int count) {
      std::string cleaned = text;
      for (auto &ch : cleaned) {
        if (ch == ',' || ch == ';' || ch == '(' || ch == ')' || ch == '[' || ch == ']') {
          ch = ' ';
        }
      }

      std::stringstream stream(cleaned);
      for (int i = 0; i < count; ++i) {
        if (!(stream >> out[i])) {
          return false;
        }
      }
      return true;
    };

    auto parseFloat = [&](const std::string &text, float &out) {
      return parseFloatList(text, &out, 1);
    };

    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImTable::start("General", nullptr)) {
        bool mixedName = false;
        std::string nameValue = selectedObjects.front()->name;
        for (size_t i = 1; i < selectedObjects.size(); ++i) {
          if (selectedObjects[i]->name != nameValue) {
            mixedName = true;
            break;
          }
        }
        if (mixedName) {
          nameValue.clear();
        }

        ImTable::add("Name");
        ImGui::PushID("Name");
        bool edited = ImGui::InputTextWithHint("##Name", mixedName ? "-" : "", &nameValue);
        handleHistory("Edit Name");
        ImGui::PopID();
        if (edited) {
          for (auto *selObj : selectedObjects) {
            selObj->name = nameValue;
          }
        }
        ImTable::end();
      }
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImTable::start("Transform", nullptr)) {
        glm::vec3 posValue = selectedObjects.front()->pos.resolve(selectedObjects.front()->propOverrides);
        glm::vec3 scaleValue = selectedObjects.front()->scale.resolve(selectedObjects.front()->propOverrides);
        glm::quat rotValue = selectedObjects.front()->rot.resolve(selectedObjects.front()->propOverrides);

        bool mixedPos[3] = {false, false, false};
        bool mixedScale[3] = {false, false, false};
        bool mixedRot[4] = {false, false, false, false};
        for (size_t i = 1; i < selectedObjects.size(); ++i) {
          auto *selObj = selectedObjects[i];
          auto pos = selObj->pos.resolve(selObj->propOverrides);
          auto scale = selObj->scale.resolve(selObj->propOverrides);
          auto rot = selObj->rot.resolve(selObj->propOverrides);

          if (!floatEqual(pos.x, posValue.x)) mixedPos[0] = true;
          if (!floatEqual(pos.y, posValue.y)) mixedPos[1] = true;
          if (!floatEqual(pos.z, posValue.z)) mixedPos[2] = true;

          if (!floatEqual(scale.x, scaleValue.x)) mixedScale[0] = true;
          if (!floatEqual(scale.y, scaleValue.y)) mixedScale[1] = true;
          if (!floatEqual(scale.z, scaleValue.z)) mixedScale[2] = true;

          if (!floatEqual(rot.x, rotValue.x)) mixedRot[0] = true;
          if (!floatEqual(rot.y, rotValue.y)) mixedRot[1] = true;
          if (!floatEqual(rot.z, rotValue.z)) mixedRot[2] = true;
          if (!floatEqual(rot.w, rotValue.w)) mixedRot[3] = true;
        }

        auto applyVec3Component = [&](Property<glm::vec3> Project::Object::*prop, int index, float value) {
          for (auto *selObj : selectedObjects) {
            bool createdOverride = false;
            glm::vec3 resolvedBefore = (selObj->*prop).resolve(selObj->propOverrides);
            if (selObj->isPrefabInstance()
                && !selObj->isPrefabEdit
                && selObj->propOverrides.find((selObj->*prop).id) == selObj->propOverrides.end()) {
              selObj->addPropOverride(selObj->*prop);
              createdOverride = true;
            }

            auto &vec = (selObj->*prop).resolve(selObj->propOverrides);
            if (createdOverride) {
              vec = resolvedBefore;
            }
            if (index == 0) vec.x = value;
            if (index == 1) vec.y = value;
            if (index == 2) vec.z = value;
          }
        };

        auto applyQuatComponent = [&](Property<glm::quat> Project::Object::*prop, int index, float value) {
          for (auto *selObj : selectedObjects) {
            bool createdOverride = false;
            glm::quat resolvedBefore = (selObj->*prop).resolve(selObj->propOverrides);
            if (selObj->isPrefabInstance()
                && !selObj->isPrefabEdit
                && selObj->propOverrides.find((selObj->*prop).id) == selObj->propOverrides.end()) {
              selObj->addPropOverride(selObj->*prop);
              createdOverride = true;
            }

            auto &quat = (selObj->*prop).resolve(selObj->propOverrides);
            if (createdOverride) {
              quat = resolvedBefore;
            }
            if (index == 0) quat.x = value;
            if (index == 1) quat.y = value;
            if (index == 2) quat.z = value;
            if (index == 3) quat.w = value;
          }
        };

        auto drawFloatField = [&](\
          const char *widgetKey,
          bool mixed,
          float &value,
          float width,
          const std::string &snapshotLabel,
          const std::function<void(float)> &applyValue
        ) {
          std::string inputId = std::string{"##Value_"} + widgetKey;
          ImGui::SetNextItemWidth(width);
          if (mixed) {
            auto &text = mixedValueCache[inputId];
            ImGui::InputTextWithHint(inputId.c_str(), "-", &text);
            handleHistory(snapshotLabel);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
              float parsed = value;
              if (parseFloat(text, parsed)) {
                value = parsed;
                applyValue(parsed);
              }
              text.clear();
            }
          } else {
            if (ImGui::InputFloat(inputId.c_str(), &value)) {
              applyValue(value);
            }
            handleHistory(snapshotLabel);
          }
        };

        ImTable::add("Pos");
        ImGui::PushID("Pos");
        float posWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        drawFloatField("PosX", mixedPos[0], posValue.x, posWidth, "Edit Pos", [&](float val) {
          applyVec3Component(&Project::Object::pos, 0, val);
        });
        ImGui::SameLine();
        drawFloatField("PosY", mixedPos[1], posValue.y, posWidth, "Edit Pos", [&](float val) {
          applyVec3Component(&Project::Object::pos, 1, val);
        });
        ImGui::SameLine();
        drawFloatField("PosZ", mixedPos[2], posValue.z, posWidth, "Edit Pos", [&](float val) {
          applyVec3Component(&Project::Object::pos, 2, val);
        });
        ImGui::PopID();

        ImTable::add("Scale");
        ImGui::PushID("Scale");
        float scaleWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        drawFloatField("ScaleX", mixedScale[0], scaleValue.x, scaleWidth, "Edit Scale", [&](float val) {
          applyVec3Component(&Project::Object::scale, 0, val);
        });
        ImGui::SameLine();
        drawFloatField("ScaleY", mixedScale[1], scaleValue.y, scaleWidth, "Edit Scale", [&](float val) {
          applyVec3Component(&Project::Object::scale, 1, val);
        });
        ImGui::SameLine();
        drawFloatField("ScaleZ", mixedScale[2], scaleValue.z, scaleWidth, "Edit Scale", [&](float val) {
          applyVec3Component(&Project::Object::scale, 2, val);
        });
        ImGui::PopID();

        ImTable::add("Rot");
        ImGui::PushID("Rot");
        float rotWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
        drawFloatField("RotX", mixedRot[0], rotValue.x, rotWidth, "Edit Rot", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 0, val);
        });
        ImGui::SameLine();
        drawFloatField("RotY", mixedRot[1], rotValue.y, rotWidth, "Edit Rot", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 1, val);
        });
        ImGui::SameLine();
        drawFloatField("RotZ", mixedRot[2], rotValue.z, rotWidth, "Edit Rot", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 2, val);
        });
        ImGui::SameLine();
        drawFloatField("RotW", mixedRot[3], rotValue.w, rotWidth, "Edit Rot", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 3, val);
        });
        ImGui::PopID();

        ImTable::end();
      }
    }

    return;
  }

  bool isPrefabInst = false;

  auto obj = scene.getObjectByUUID(selectedIds.front());
  if (!obj) {
    selection.clear();
    return;
  }
  if (selection.primary() != obj->uuid) {
    selection.set(obj->uuid);
  }

  Project::Object* srcObj = obj.get();
  std::shared_ptr<Project::Prefab> prefab{};
  if(obj->uuidPrefab.value)
  {
    prefab = ctx.project->getAssets().getPrefabByUUID(obj->uuidPrefab.value);
    if(prefab)srcObj = &prefab->obj;
    isPrefabInst = true;
  }


  //if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if (ImTable::start("General", obj.get())) {
      ImTable::add("Name", obj->name);

      int idProxy = obj->id;
      ImTable::add("ID", idProxy);
      obj->id = static_cast<uint16_t>(idProxy);

      //ImTable::add("UUID");
      //ImGui::Text("0x%16lX", obj->uuid);

      if(isPrefabInst) {
        ImTable::add("Prefab");

        auto name = std::string{ICON_MDI_PENCIL " "};
        name += obj->isPrefabEdit ? ("Back to Instance") : ("Edit '" + srcObj->name + "'");

        if(ImGui::Button(name.c_str())) {
          if (obj->isPrefabEdit) {
            ctx.project->getAssets().markPrefabDirty(prefab->uuid.value);
          }
          obj->isPrefabEdit = !obj->isPrefabEdit;
        }
      }

      ImTable::end();
    }
  }

  if(ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if(ImTable::start("Transform", obj.get()))
    {
      ImTable::addObjProp("Pos", srcObj->pos);

      if(srcObj->proportionalScale)
      {
        std::function<bool(glm::vec3*)> cb = [](glm::vec3 *val) -> bool {
          glm::vec3 scale = *val;
          if (scale == glm::vec3(0,0,0)) {
            if (!ImGui::InputFloat3("##", glm::value_ptr(*val))) return false;
            *val = glm::vec3(val->x + val->y + val->z);
            return true;
          }
          ImGuiContext& g = *GImGui;
          ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
          float ratio = 1.0f;
          for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            if (i > 0) ImGui::SameLine(0, g.Style.ItemInnerSpacing.x);
            bool isZero = glm::abs(scale[i]) < 0.0001f;
            if (isZero) ImGui::BeginDisabled();
            if (ImGui::InputFloat("", &(*val)[i])) ratio = (*val)[i] / scale[i];
            if (isZero) ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::PopItemWidth();
          }
          *val = scale * ratio;
          return ratio != 1.0f;
        };
        ImTable::addObjProp("Scale", srcObj->scale, cb, nullptr);
      } else {
        ImTable::addObjProp("Scale", srcObj->scale);
      }

      // icon to toggle between proportional and independent scale
      ImGui::SameLine();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 32_px);
      if(ImGui::IconButton(srcObj->proportionalScale ? ICON_MDI_LINK_VARIANT : ICON_MDI_LINK_VARIANT_OFF, {24_px, 24_px})) {
        ImGui::ClearActiveID();
        srcObj->proportionalScale = !srcObj->proportionalScale;
      }
      ImGui::SetItemTooltip(srcObj->proportionalScale
        ? "Change to Independent Scale"
        : "Change to Proportional Scale"
      );

      ImTable::addObjProp("Rot", srcObj->rot);

      // icon to toggle between quaternion and euler
      ImGui::SameLine();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 32_px);
      if(ImGui::IconButton(ctx.prefs.showRotAsEuler ? ICON_MDI_AXIS_Z_ROTATE_CLOCKWISE : ICON_MDI_SPHERE, {24_px, 24_px})) {
        ImGui::ClearActiveID();
        ctx.prefs.showRotAsEuler = !ctx.prefs.showRotAsEuler;
        ctx.prefs.save();
      }
      ImGui::SetItemTooltip(ctx.prefs.showRotAsEuler
        ? "Change to Quaternion"
        : "Change to Euler (degrees)"
      );

      ImTable::end();
    }
  }

  // 2D / Canvas section: surfaces the screen-space pass routing knobs only
  // when the object itself is a Canvas root or sits under one. Hidden on
  // pure 3D objects so the inspector stays uncluttered for the common case.
  bool inCanvas2D = obj->isCanvas2D;
  for(auto *p = obj->parent; p && !inCanvas2D; p = p->parent) {
    if(p->isCanvas2D) inCanvas2D = true;
  }
  if (obj->isCanvas2D || inCanvas2D)
  {
    if (ImGui::CollapsingHeader("2D / Canvas", ImGuiTreeNodeFlags_DefaultOpen))
    {
      if (ImTable::start("Canvas2D", obj.get()))
      {
        ImTable::add("Canvas Root");
        bool isRoot = obj->isCanvas2D;
        if (ImGui::Checkbox("##canvasRoot", &isRoot)) {
          obj->isCanvas2D = isRoot;
          UndoRedo::getHistory().markChanged("Toggle Canvas Root");
        }
        ImGui::SetItemTooltip(
          "Marks this Object as the start of a 2D screen-space subtree.\n"
          "All descendants render in the 2D pass instead of the 3D pass.");

        ImTable::add("Anchor");
        // 3x3 anchor grid; selected cell is highlighted. UE/Godot pattern.
        const char *anchorNames[9] = {
          "TL","TC","TR",
          "ML","C ","MR",
          "BL","BC","BR",
        };
        for (int i = 0; i < 9; ++i) {
          if (i % 3 != 0) ImGui::SameLine(0.0f, 2.0f);
          ImGui::PushID(i);
          bool sel = (obj->anchor2D == (uint8_t)i);
          if (ImGui::Selectable(anchorNames[i], sel,
                ImGuiSelectableFlags_DontClosePopups, ImVec2(20.0f, 18.0f))) {
            obj->anchor2D = (uint8_t)i;
            UndoRedo::getHistory().markChanged("Edit Anchor");
          }
          ImGui::PopID();
        }
        ImGui::SetItemTooltip(
          "Anchor inside the framebuffer. Object's pos is added to the\n"
          "anchor origin at build time.");

        ImTable::add("Layer Idx (2D)");
        int layer = (int)obj->layerIndex2D;
        if (ImGui::DragInt("##layer2D", &layer, 0.1f, 0, 15)) {
          if (layer < 0) layer = 0;
          if (layer > 255) layer = 255;
          obj->layerIndex2D = (uint8_t)layer;
          UndoRedo::getHistory().markChanged("Edit 2D Layer Index");
        }
        ImGui::SetItemTooltip(
          "Which 2D rspq queue this Object's components render into.\n"
          "Higher numbers draw on top of lower ones. The scene config's\n"
          "layerCount2D bounds this — values past it are ignored.");

        ImTable::end();
      }
    }
  }

  // Prefab class variables: when this object is an instance of a prefab that
  // defines class-level variables, render an editable row per variable. Edits
  // write into obj->varOverrides keyed by the variable def's stable uuid.
  // The class default lives on prefab->variables[i].defaultValue and is the
  // fallback when no override exists yet.
  if (isPrefabInst && prefab && !prefab->variables.empty()) {
    if (ImGui::CollapsingHeader("Prefab Variables", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImTable::start("PrefabVars", obj.get())) {
        for (const auto &def : prefab->variables) {
          ImTable::add(def.name);
          ImGui::PushID(static_cast<int>(def.uuid));
          ImGui::SetNextItemWidth(-1);

          // Pull current effective value: override if present, otherwise the
          // class default. We materialize a copy on first edit (the line below
          // each kind branch) so the override map only grows when the user
          // actually touches the field.
          auto it = obj->varOverrides.find(def.uuid);
          GenericValue effective = (it != obj->varOverrides.end())
            ? it->second : def.defaultValue;

          bool changed = false;
          switch (def.kind) {
            case Project::PrefabVarKind::INT: {
              int val = effective.get<int32_t>();
              if (ImGui::DragInt("##v", &val)) { effective.set<int32_t>(val); changed = true; }
              break;
            }
            case Project::PrefabVarKind::FLOAT: {
              float val = effective.get<float>();
              if (ImGui::DragFloat("##v", &val, 0.01f)) { effective.set<float>(val); changed = true; }
              break;
            }
            case Project::PrefabVarKind::BOOL: {
              bool val = effective.get<bool>();
              if (ImGui::Checkbox("##v", &val)) { effective.set<bool>(val); changed = true; }
              break;
            }
            case Project::PrefabVarKind::VEC3: {
              glm::vec3 val = effective.get<glm::vec3>();
              if (ImGui::DragFloat3("##v", &val.x, 0.01f)) { effective.set<glm::vec3>(val); changed = true; }
              break;
            }
            case Project::PrefabVarKind::QUAT: {
              glm::quat q = effective.get<glm::quat>();
              float xyzw[4]{q.x, q.y, q.z, q.w};
              if (ImGui::DragFloat4("##v", xyzw, 0.01f)) {
                effective.set<glm::quat>(glm::quat{xyzw[3], xyzw[0], xyzw[1], xyzw[2]});
                changed = true;
              }
              break;
            }
            case Project::PrefabVarKind::OBJECT_REF: {
              uint64_t val = effective.get<uint64_t>();
              auto target = scene.getObjectByUUID(static_cast<uint32_t>(val));
              std::string label = target ? target->name : "(null)";
              if (ImGui::BeginCombo("##v", label.c_str())) {
                if (ImGui::Selectable("(null)", val == 0)) {
                  effective.set<uint64_t>(0);
                  changed = true;
                }
                for (const auto &candidate : scene.getRootObject().children) {
                  if (!candidate) continue;
                  bool sel = (candidate->uuid == val);
                  std::string entryLabel = candidate->name + "##" + std::to_string(candidate->uuid);
                  if (ImGui::Selectable(entryLabel.c_str(), sel)) {
                    effective.set<uint64_t>(candidate->uuid);
                    changed = true;
                  }
                }
                ImGui::EndCombo();
              }
              break;
            }
            case Project::PrefabVarKind::PREFAB_REF: {
              // typeArg is the target prefab uuid; the override stores the
              // referenced object's runtime uuid (resolved at scene-build time
              // against the active scene's prefab instances).
              uint64_t val = effective.get<uint64_t>();
              auto target = scene.getObjectByUUID(static_cast<uint32_t>(val));
              std::string label = target ? target->name : "(null)";
              if (ImGui::BeginCombo("##v", label.c_str())) {
                if (ImGui::Selectable("(null)", val == 0)) {
                  effective.set<uint64_t>(0);
                  changed = true;
                }
                for (const auto &candidate : scene.getRootObject().children) {
                  if (!candidate) continue;
                  if (candidate->uuidPrefab.value != def.typeArg) continue;
                  bool sel = (candidate->uuid == val);
                  std::string entryLabel = candidate->name + "##" + std::to_string(candidate->uuid);
                  if (ImGui::Selectable(entryLabel.c_str(), sel)) {
                    effective.set<uint64_t>(candidate->uuid);
                    changed = true;
                  }
                }
                ImGui::EndCombo();
              }
              break;
            }
            case Project::PrefabVarKind::ASSET_REF: {
              ImGui::TextDisabled("(asset ref - TODO)");
              break;
            }
          }
          ImGui::PopID();

          if (changed) {
            UndoRedo::getHistory().markChanged("Set Prefab Variable");
            obj->varOverrides[def.uuid] = effective;
          }
        }
        ImTable::end();
      }
    }
  }

  uint64_t compDelUUID = 0;
  Project::Component::Entry *compCopy = nullptr;

  auto drawComp = [&](Project::Object* obj, Project::Component::Entry &comp, bool isInstance)
  {
    ImTable::PrefabEditScope prefabScope(isInstance);
    ImGui::PushID(&comp);

    auto &def = Project::Component::TABLE[comp.id];
    auto name = std::string{def.icon} + "  " + comp.name;
    if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
      if(!ImTable::isPrefabLocked(obj))
      {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
          ImGui::OpenPopup("CompCtx");
        }

        if(ImGui::BeginPopupContextItem("CompCtx"))
        {
          if (ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Duplicate")) {
            compCopy = &comp;
          }
          if (ImGui::MenuItem(ICON_MDI_TRASH_CAN_OUTLINE " Delete")) {
            compDelUUID = comp.uuid;
          }
          ImGui::EndPopup();
        }
      }

      def.funcDraw(*obj, comp);
    }
    ImGui::PopID();
  };

  for (auto &comp : srcObj->components) {
    drawComp(obj.get(), comp, false);
  }

  if(isPrefabInst && !obj->isPrefabEdit) {
    for (auto &comp : obj->components) {
      drawComp(obj.get(), comp, true);
    }
    srcObj = obj.get();
  }

  if (isPrefabInst && obj->isPrefabEdit && prefab) {
    ctx.project->getAssets().markPrefabDirty(prefab->uuid.value);
  }

  if (compCopy) {
    const int compCopyId = compCopy->id;
    const std::string compCopyName = compCopy->name;
    UndoRedo::getHistory().markChanged("Duplicate Component");
    srcObj->addComponent(compCopyId);
    srcObj->components.back().name = compCopyName + " Copy";
  }
  if (compDelUUID) {
    UndoRedo::getHistory().markChanged("Delete Component");
    srcObj->removeComponent(compDelUUID);
  }

  const char* addLabel = ICON_MDI_PLUS_BOX_OUTLINE " Add Component";
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4_px);
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(addLabel).x) * 0.5f - 4_px);
  if (ImGui::Button(addLabel)) {
    ImGui::OpenPopup("CompSelect");
  }

  if (ImGui::BeginPopupContextItem("CompSelect"))
  {
    for (auto &comp : Project::Component::TABLE_SORTED_BY_NAME) {
      auto name = std::string{comp.icon} + " " + comp.name;
      if(ImGui::MenuItem(name.c_str())) {
        UndoRedo::getHistory().markChanged("Add Component");
        srcObj->addComponent(comp.id);
      }
    }
    ImGui::EndPopup();
  }
}
