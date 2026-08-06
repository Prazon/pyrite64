/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "objectInspector.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "imgui_internal.h"
#include "../../imgui/helper.h"
#include "../../transformUtils.h"
#include "../../../context.h"
#include "../../../project/component/components.h"
#include "../../../project/scene/scene.h"
#include "../../../project/selection.h"
#include "../../selectionUtils.h"
#include "../../undoRedo.h"

namespace
{
  constexpr int COMP_ID_CODE = 0;

  struct ComponentDragPayload
  {
    Project::Object *owner{};
    uint64_t compUUID{};
  };

  struct ComponentReorderRequest
  {
    Project::Object *owner{};
    uint64_t compUUID{};
    size_t insertIndex{};
  };

  /**
   * Returns whether the current drag payload is an object-script asset.
   * @param scriptUUID Optional output for the dragged script UUID.
   * @return true when the active payload is a CODE_OBJ asset.
   */
  bool isDraggedObjectScript(uint64_t *scriptUUID = nullptr)
  {
    // Access ImGui's drag-drop state
    ImGuiContext &g = *GImGui;
    // There is no drag operation or no project assets to inspect --> Do nothing
    if (!g.DragDropActive || !ctx.project) return false;

    // Only asset payloads with UUIDs can create Code components here
    const ImGuiPayload *payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType("ASSET") || payload->DataSize != sizeof(uint64_t))
      return false;

    // Resolve the dragged UUID back to an asset entry and verify that it is an object script
    const uint64_t uuid = *static_cast<const uint64_t*>(payload->Data);
    auto *script = ctx.project->getAssets().getEntryByUUID(uuid);
    if (!script || script->type != Project::FileType::CODE_OBJ)
      return false;

    // Requeted to output the UUID --> Assign it
    if (scriptUUID)
      *scriptUUID = uuid;

    return true;
  }

  /**
   * Creates a Code component using dropped Script.
   * @param targetObj Object receiving the new component.
   * @param scriptUUID UUID of the dragged script.
   * @return true when the component was added successfully.
   */
  bool createCodeComponentFromScript(Project::Object *targetObj, uint64_t scriptUUID)
  {
    // There is no target object --> Do nothing
    if (!targetObj) return false;

    // Track the component count to detect whether addComponent actually appended one
    const auto oldSize = targetObj->components.size();
    // Record undo history before modifying the component list
    Editor::UndoRedo::getHistory().markChanged("Add Code Component");
    // Create a Code component
    targetObj->addComponent(COMP_ID_CODE);
    if (targetObj->components.size() <= oldSize) return false;

    // Set the Script field for the created Code component
    auto &comp = targetObj->components.back();
    Project::Component::Code::setScript(comp, scriptUUID, false);
    return true;
  }

  /**
   * Draws a thin insertion target for component reordering.
   * @param owner Object owning the components.
   * @param insertIndex Target insertion index in the owner component list.
   * @param previousComponentCollapsed true when the component above the index is folded (spacing is smaller).
   * @param reorderRequest Output request filled when a component is dropped here.
   */
  void drawComponentInsertTarget(
    Project::Object *owner,
    size_t insertIndex,
    bool previousComponentCollapsed,
    ComponentReorderRequest &reorderRequest
  ) {
    // No drag operation active --> Abort
    if (!owner || !ImGui::IsDragDropActive()) return;

    // Center the hit area on the current layout boundary without changing layout
    const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    ImGuiWindow *window = ImGui::GetCurrentWindowRead();
    const ImGuiStyle &style = ImGui::GetStyle();
    const float headerOuterExtend = IM_TRUNC(window->WindowPadding.x * 0.5f);
    const float minX = cursorScreen.x - headerOuterExtend;
    const float maxX = window->WorkRect.Max.x + headerOuterExtend;
    const float markerMargin = 1.0f; // Vertical margin between the marker and the components
    const float markerHeight = style.ItemSpacing.y - markerMargin * 2.0f; // Make marker fit height, extra margin top and bottom
    const float hitHeight = 18.0f; // Vertical space where the drop between component is accepted
    const float markerY = cursorScreen.y - style.ItemSpacing.y * 0.5f - markerMargin + (previousComponentCollapsed ? 1.0f : 0.0f); // Y start position of the marker
    const ImRect hitRect{
      ImVec2{minX, markerY - hitHeight * 0.5f},
      ImVec2{maxX, markerY + hitHeight * 0.5f}
    };

    // Register an explicit drop target that does not draw ImGui's default rectangle
    ImGui::PushID(owner);
    ImGui::PushID(static_cast<int>(insertIndex));
    if (ImGui::BeginDragDropTargetCustom(hitRect, ImGui::GetID("##ComponentInsertTarget"))) {
      const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
        "COMPONENT",
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
      );

      // Only components from the same owner can be reordered within this list
      if (payload && payload->DataSize == sizeof(ComponentDragPayload)) {
        const auto &dragPayload = *static_cast<const ComponentDragPayload*>(payload->Data);
        if (dragPayload.owner == owner) {
          const ImU32 markerColor = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
          ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2{minX, markerY - markerHeight * 0.5f},
            ImVec2{maxX, markerY + markerHeight * 0.5f},
            markerColor,
            0.0f
          );

          // Mouse released over this boundary --> Store the reorder request
          if (payload->Delivery) {
            reorderRequest.owner = owner;
            reorderRequest.compUUID = dragPayload.compUUID;
            reorderRequest.insertIndex = insertIndex;
          }
        }
      }

      ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    ImGui::PopID();
  }

  /**
   * Moves a component to an index within its owner.
   * @param owner Object whose component list should be reordered.
   * @param compUUID UUID of the component to move.
   * @param insertIndex Target index before removing the source component.
   * @return true when the component order changed.
   */
  bool reorderComponent(Project::Object *owner, uint64_t compUUID, size_t insertIndex)
  {
    // There is no owner --> Abort
    if (!owner) return false;

    // Find the source component in the list of the owner
    auto &components = owner->components;
    auto it = std::find_if(
      components.begin(),
      components.end(),
      [compUUID](const Project::Component::Entry &entry) {
        return entry.uuid == compUUID;
      }
    );
    // Cannot find the component --> Abort
    if (it == components.end()) return false;

    // Clamp the insertion index to the list size
    insertIndex = std::min(insertIndex, components.size());

    // Ignore drops that keep the component in the same place
    const size_t sourceIndex = static_cast<size_t>(std::distance(components.begin(), it));
    if (insertIndex == sourceIndex || insertIndex == sourceIndex + 1) return false;

    // Move the component entry while preserving its UUID and data pointer
    auto moving = std::move(*it);
    components.erase(it);

    // Removing an earlier element shifts later insertion boundaries one slot left
    if (sourceIndex < insertIndex) --insertIndex;
    components.insert(components.begin() + static_cast<std::ptrdiff_t>(insertIndex), std::move(moving));
    
    return true;
  }

  /**
   * Handles dropping an object script onto the inspector panel.
   * @param targetObj Object that should receive a new Code component.
   * @param dropRect Custom panel-space drop rectangle.
   * @param highlightWindow True to draw the inspector-wide highlight border.
   * @return True when a valid script is hovering or was delivered to this target.
   */
  bool handleScriptComponentDropTarget(Project::Object *targetObj, const ImRect &dropRect, bool highlightWindow)
  {
    // Register the inspector panel as a custom drop target covering the requested rectangle
    if (!ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("##ScriptComponentDropTarget"))) return false;

    // Peek the asset payload while it hovers so the panel can highlight before delivery
    const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
      "ASSET",
      ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
    );
    // There is no compatible payload --> Skip the target
    if (!payload || payload->DataSize != sizeof(uint64_t)) {
      ImGui::EndDragDropTarget();
      return false;
    }

    // Resolve the hovered asset UUID and reject anything that is not an object Script
    const uint64_t scriptUUID = *static_cast<const uint64_t*>(payload->Data);
    auto *script = ctx.project->getAssets().getEntryByUUID(scriptUUID);
    // Is not a Code component --> Skip the target
    if (!script || script->type != Project::FileType::CODE_OBJ) {
      ImGui::EndDragDropTarget();
      return false;
    }

    if (highlightWindow) {
      // Use the window's inner rectangle so the border matches the visible inspector panel
      ImGuiWindow *window = ImGui::GetCurrentWindowRead();
      auto col = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
      ImRect borderRect = window->InnerRect;
      // Fix the rect size not to write outside of the panel
      borderRect.Min.y += 1.0f;
      borderRect.Min.x += 2.0f;
      borderRect.Max.x -= 2.0f;
      borderRect.Max.y -= 2.0f;
      // Draw an outline to highlight the object inspector panel
      ImGui::GetWindowDrawList()->AddRect(borderRect.Min, borderRect.Max, col, 0.0f, 0, 2.0f);
    }

    bool accepted = false;
    // Released the mouse --> Commit drop, create Code component
    if (payload->Delivery) {
      accepted = createCodeComponentFromScript(targetObj, scriptUUID);
    // Hovering valid script --> Mark the panel as active drop target
    } else {
      accepted = true;
    }

    // Close the custom target scope opened above
    ImGui::EndDragDropTarget();
    return accepted;
  }
}

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
                && !ctx.isPrefabEditing(selObj->uuid)
                && !selObj->hasPropOverride(selObj->*prop)) {
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
                && !ctx.isPrefabEditing(selObj->uuid)
                && !selObj->hasPropOverride(selObj->*prop)) {
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

        ImTable::add("Position");
        ImGui::PushID("Position");
        float posWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        drawFloatField("PosX", mixedPos[0], posValue.x, posWidth, "Edit Position", [&](float val) {
          applyVec3Component(&Project::Object::pos, 0, val);
        });
        ImGui::SameLine();
        drawFloatField("PosY", mixedPos[1], posValue.y, posWidth, "Edit Position", [&](float val) {
          applyVec3Component(&Project::Object::pos, 1, val);
        });
        ImGui::SameLine();
        drawFloatField("PosZ", mixedPos[2], posValue.z, posWidth, "Edit Position", [&](float val) {
          applyVec3Component(&Project::Object::pos, 2, val);
        });
        ImGui::PopID();

        ImTable::add("Rotation");
        ImGui::PushID("Rotation");
        float rotWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
        drawFloatField("RotX", mixedRot[0], rotValue.x, rotWidth, "Edit Rotation", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 0, val);
        });
        ImGui::SameLine();
        drawFloatField("RotY", mixedRot[1], rotValue.y, rotWidth, "Edit Rotation", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 1, val);
        });
        ImGui::SameLine();
        drawFloatField("RotZ", mixedRot[2], rotValue.z, rotWidth, "Edit Rotation", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 2, val);
        });
        ImGui::SameLine();
        drawFloatField("RotW", mixedRot[3], rotValue.w, rotWidth, "Edit Rotation", [&](float val) {
          applyQuatComponent(&Project::Object::rot, 3, val);
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

  // When a nested prefab object is selected (selSubPath), the inspector shows that node
  // instead of the root instance, rendered through the same path below. The guards stay
  // alive in 'nested' so its edits keep authoring to the right override owner.
  Editor::SelectionUtils::NestedTarget nested;
  if(isPrefabInst) Editor::SelectionUtils::resolveNestedTarget(nested, obj.get(), prefab.get());

  // The inspected target: the nested node when one is selected, else the root object.
  Project::Object* tableObj = nested.isNested ? nested.node : obj.get();    // backs the override map
  Project::Object* xfSrc    = nested.isNested ? nested.node : srcObj;       // transform property source
  Project::Object* compSrc  = nested.isNested ? nested.nodeSrc : srcObj;    // component source
  // A nested override authors via the active cascade (Path), the root and a direct
  // definition edit author via a fresh component layer (Dispatch).
  bool compViaPath = nested.isNested && !nested.directDefEdit;


  {
    if (ImTable::start("General", tableObj)) {
      if(nested.isNested) {
        // A nested node's name belongs to the prefab definition, so it is read-only here.
        ImTable::add("Name");
        ImGui::TextUnformatted(nested.node->name.c_str());
      } else {
        // The name belongs to the object itself, not the prefab, so it stays editable even
        // on a locked prefab instance.
        // Build this row manually so the object-enabled checkbox sits before the Name label
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID("Name");
        // Edit a local copy so undo captures the scene before Object::enabled is changed
        bool enabled = obj->enabled;
        if (ImGui::Checkbox("##Enabled", &enabled)) {
          Editor::UndoRedo::getHistory().markChanged(enabled ? "Enable Object" : "Disable Object");
          obj->enabled = enabled;
        }
        ImGui::SetItemTooltip("%s Object", obj->enabled ? "Disable" : "Enable");
        // Reuse checkbox width and spacing to align following labels with the Name text
        const float objectLabelOffset = ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        // The editable name itself remains in the table's value column
        ImGui::TableSetColumnIndex(1);
        if(ImGui::InputText("##Name", &obj->name)) {
          Editor::UndoRedo::getHistory().markChanged("Edit Name");
        }
        ImGui::PopID();

        if(isPrefabInst) {
          // Leave the checkbox column empty so Prefab starts below Name
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + objectLabelOffset);
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted("Prefab");
          ImGui::TableSetColumnIndex(1);

          bool editing = ctx.isPrefabEditing(obj->uuid);
          auto name = std::string{ICON_MDI_PENCIL " "};
          name += editing ? ("Back to Instance") : ("Edit '" + srcObj->name + "'");

          if(ImGui::Button(name.c_str())) {
            if (editing) {
              ctx.project->getAssets().markPrefabDirty(prefab->uuid.value);
              ctx.prefabEditUUID = 0;
            } else {
              ctx.prefabEditUUID = obj->uuid;
            }
          }
        }

        ImTable::addMultiSelectMask8("Visibility", obj->visMask.resolve(obj->propOverrides),
          ctx.project->conf.visLayerNames, "<Hidden>");
      }

      ImTable::end();
    }
  }

  if(ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if(ImTable::start("Transform", tableObj))
    {
      ImTable::addObjProp(
        "Position",
        xfSrc->pos,
        Editor::TransformUtils::preserveChildTransformsDuringEdit<glm::vec3>(obj.get(), [](glm::vec3 *val) -> bool {
          // Use the standard vector editor while preserving child offsets
          return ImTable::typedInput<glm::vec3>(val);
        }),
        nullptr
      );

      ImTable::addObjProp(
        "Rotation",
        xfSrc->rot,
        Editor::TransformUtils::preserveChildTransformsDuringEdit<glm::quat>(obj.get(), [](glm::quat *val) -> bool {
          // Use the standard quaternion editor while preserving child offsets
          return ImTable::typedInput<glm::quat>(val);
        }),
        nullptr
      );

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

      if(xfSrc->proportionalScale)
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
        ImTable::addObjProp(
          "Scale",
          xfSrc->scale,
          Editor::TransformUtils::preserveChildTransformsDuringEdit<glm::vec3>(obj.get(), cb),
          nullptr
        );
      } else {
        ImTable::addObjProp(
          "Scale",
          xfSrc->scale,
          Editor::TransformUtils::preserveChildTransformsDuringEdit<glm::vec3>(obj.get(), [](glm::vec3 *val) -> bool {
            // Use the standard vector editor while preserving child offsets
            return ImTable::typedInput<glm::vec3>(val);
          }),
          nullptr
        );
      }

      // icon to toggle between proportional and independent scale
      ImGui::SameLine();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 32_px);
      if(ImGui::IconButton(xfSrc->proportionalScale ? ICON_MDI_LINK_VARIANT : ICON_MDI_LINK_VARIANT_OFF, {24_px, 24_px})) {
        ImGui::ClearActiveID();
        xfSrc->proportionalScale = !xfSrc->proportionalScale;
      }
      ImGui::SetItemTooltip(xfSrc->proportionalScale
        ? "Change to Independent Scale"
        : "Change to Proportional Scale"
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
            case Project::PrefabVarKind::ARRAY: {
              // Per-instance ARRAY overrides aren't authored in the
              // inspector. The class-default is always empty; populate
              // at runtime via ArrayMake or ArrayPush.
              ImGui::TextDisabled("(empty — runtime-populated)");
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
  // Store a deferred reorder request so the component vector is not modified while drawing
  ComponentReorderRequest compReorder{};

  // viaPath: resolve component props through the active nested cascade (Path) rather than
  // a fresh component layer (Dispatch). True only for a nested override target.
  // Draw the component and return whether its header is open for the next insertion marker
  auto drawComp = [&](Project::Object* obj, Project::Object *owner, Project::Component::Entry &comp, bool isInstance, bool viaPath) -> bool
  {
    ImTable::PrefabEditScope prefabScope(isInstance);
    ImGui::PushID(&comp);

    // Keep component path active for both its header state and its regular fields. On prefab instances applies as an override
    std::optional<PropScope::Dispatch> dispatch;
    std::optional<PropScope::Path> compPath;
    if(viaPath) compPath.emplace(comp.uuid);
    else        dispatch.emplace(obj->propOverrides, comp.uuid);

    auto &def = Project::Component::TABLE[comp.id];
    auto name = std::string{def.icon} + "  " + comp.name;

    bool headerOpen = ImGui::CollapsingHeader(
      "##ComponentHeader",
      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap
    );
    const ImVec2 headerMin = ImGui::GetItemRectMin();
    const ImVec2 headerMax = ImGui::GetItemRectMax();
    const ImVec2 cursorAfterHeader = ImGui::GetCursorScreenPos();
    const bool locked = ImTable::isPrefabLocked(obj);
    const bool headerRightClicked = !locked && ImGui::IsItemClicked(ImGuiMouseButton_Right);

    // Starting to drag
    if (!locked && ImGui::BeginDragDropSource()) {
      // Start component reordering from the dragged header
      ComponentDragPayload payload{
        .owner = owner,
        .compUUID = comp.uuid
      };
      ImGui::SetDragDropPayload("COMPONENT", &payload, sizeof(payload));
      ImGui::TextUnformatted(name.c_str());
      ImGui::EndDragDropSource();
    }

    // Place the component toggle inside the header, between its folding arrow and icon
    const float checkboxMargin = 2_px; // Checkbox margin, so doesn't fit full header height
    const float checkboxSize = (headerMax.y - headerMin.y) - checkboxMargin * 2.0f;
    const float checkboxPaddingY = std::max(0.0f, (checkboxSize - ImGui::GetFontSize()) * 0.5f);
    // Position checkbox centered vertically inside the header
    ImGui::SetCursorScreenPos({
      headerMin.x + ImGui::GetTreeNodeToLabelSpacing(),
      headerMin.y + checkboxMargin
    });
    // Resolve through the active component path so prefab-instance overrides are shown
    bool enabled = comp.enabled.resolve(*obj);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {
      ImGui::GetStyle().FramePadding.x,
      checkboxPaddingY
    });
    if (ImGui::Checkbox("##Enabled", &enabled)) {
      // Snapshot the scene before writing either the base value or an instance override
      Editor::UndoRedo::getHistory().markChanged(enabled ? "Enable Component" : "Disable Component");
      // Locked prefab components cannot alter their definition; create an override slot
      // on the inspected instance before assigning the newly selected value
      if(locked && !obj->hasPropOverride(comp.enabled)) {
        obj->addPropOverride(comp.enabled);
      }
      // Prefab instances write through the resolved override; regular objects own the base
      if (locked)
        comp.enabled.resolve(*obj) = enabled;
      else
        comp.enabled.value = enabled;
    }
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("%s Component", enabled ? "Disable" : "Enable");

    // The collapsing header uses a hidden label, so draw the icon and component name
    // manually after the checkbox and tint them when the component is disabled
    const ImVec2 checkMax = ImGui::GetItemRectMax();
    const ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
    ImGui::GetWindowDrawList()->AddText(
      {checkMax.x + ImGui::GetStyle().ItemInnerSpacing.x,
       headerMin.y + (headerMax.y - headerMin.y - textSize.y) * 0.5f},
      ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled),
      name.c_str()
    );
    // Restore the cursor so this overlaid header content does not affect following layout
    ImGui::SetCursorScreenPos(cursorAfterHeader);

    // Faint help icon near the right edge of the header
    if (def.docSlug && def.docSlug[0]) {
      const float helpSize = 19_px;
      ImGui::SameLine(ImGui::GetContentRegionMax().x - helpSize - 4_px);
      ImGui::HelpIcon(def.docSlug, "Open Docs", helpSize);
    }

    if (headerOpen)
    {
      if(!locked)
      {
        if (headerRightClicked) {
          ImGui::OpenPopup("CompCtx");
        }

        if(ImGui::BeginPopup("CompCtx"))
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
    // Let the next insertion marker know if the previous component is folded
    return headerOpen;
  };

  // Track whether the component above the current insertion boundary is folded
  bool previousComponentCollapsed = false;
  for (size_t i = 0; i < compSrc->components.size(); ++i) {
    // Draw the insertion boundary before this component
    drawComponentInsertTarget(compSrc, i, previousComponentCollapsed, compReorder);
    // Draw the component and cache its folded state for the next boundary
    previousComponentCollapsed = !drawComp(tableObj, compSrc, compSrc->components[i], false, compViaPath);
  }
  // Draw the insertion boundary after the last component
  drawComponentInsertTarget(compSrc, compSrc->components.size(), previousComponentCollapsed, compReorder);

  // Components added directly to a scene instance (not nested, not in edit mode).
  if(!nested.isNested && isPrefabInst && !ctx.isPrefabEditing(obj->uuid)) {
    // Instance-owned components form a separate reorder list
    previousComponentCollapsed = false;
    for (size_t i = 0; i < obj->components.size(); ++i) {
      // Draw the insertion boundary before this instance-owned component
      drawComponentInsertTarget(obj.get(), i, previousComponentCollapsed, compReorder);
      // Draw the component and store its folded state for the next boundary
      previousComponentCollapsed = !drawComp(obj.get(), obj.get(), obj->components[i], true, false);
    }
    // Draw the insertion boundary after the last instance-owned component
    drawComponentInsertTarget(obj.get(), obj->components.size(), previousComponentCollapsed, compReorder);
    srcObj = obj.get();
  }

  // Apply the queued move after all component lists have finished drawing
  if (compReorder.owner) {
    if (reorderComponent(compReorder.owner, compReorder.compUUID, compReorder.insertIndex)) {
      UndoRedo::getHistory().markChanged("Move Component");
    }
  }

  if (isPrefabInst && ctx.isPrefabEditing(obj->uuid) && prefab) {
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

  // Adding components targets the root object, not a nested def node.
  if(!nested.isNested)
  {
    const char* addLabel = ICON_MDI_PLUS_BOX_OUTLINE " Add Component";
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4_px);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(addLabel).x) * 0.5f - 4_px);

    const bool compLimitReached = srcObj->components.size() >= Project::Object::MAX_COMPONENTS;
    if (compLimitReached) ImGui::BeginDisabled();
    if (ImGui::Button(addLabel)) {
      ImGui::OpenPopup("CompSelect");
    }
    if (compLimitReached) {
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Component limit reached (max. 255)");
      }
    }

    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 contentRegionMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 contentRegionMax = ImGui::GetWindowContentRegionMax();
    const ImVec2 contentMin{windowPos.x + contentRegionMin.x, windowPos.y + contentRegionMin.y};
    const ImVec2 contentMax{windowPos.x + contentRegionMax.x, windowPos.y + contentRegionMax.y};

    ImRect panelDropRect{contentMin, contentMax};
    handleScriptComponentDropTarget(srcObj, panelDropRect, true);

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
}
