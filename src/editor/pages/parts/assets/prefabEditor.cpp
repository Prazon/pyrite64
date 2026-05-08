// added by SPBF64 fork
#include "prefabEditor.h"

#include <cstdio>

#include "imgui_internal.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/hash.h"
#include "../../../../utils/logger.h"
#include "../../../imgui/helper.h"
#include "../../editorScene.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{720, 540};
  constexpr float MIN_PANE_WIDTH = 120.0f;
  constexpr float SPLITTER_WIDTH = 4.0f;

  // Stable string form of the variables list, used purely for dirty detection
  // inside the editor. Mirrors the on-disk format from Prefab::serialize, but
  // we keep a separate copy here so the editor doesn't have to round-trip the
  // whole Prefab to compare. Field order matters: keep aligned with prefab.cpp.
  std::string varsToJSONString(const std::vector<Project::PrefabVarDef> &vars)
  {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &v : vars) {
      arr.push_back({
        {"uuid", v.uuid},
        {"name", v.name},
        {"kind", static_cast<uint8_t>(v.kind)},
        {"typeArg", v.typeArg},
        {"default", v.defaultValue.serialize()},
      });
    }
    return arr.dump();
  }
}

Editor::PrefabEditor::PrefabEditor(uint64_t uuid) : assetUUID(uuid)
{
  // Prefab editor leans on the UE5 Components panel convention: each Object
  // shows its attached components as leaf children, so the user can scan
  // what's on every node without having to click through them.
  graph.showComponentsInline = true;
  loadFromDisk();
}

void Editor::PrefabEditor::loadFromDisk()
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB || !asset->prefab) {
    Utils::Logger::log(
      "PrefabEditor: asset " + std::to_string(assetUUID) + " is not a prefab",
      Utils::Logger::LEVEL_ERROR
    );
    return;
  }
  filePath = asset->path;

  // Snapshot the prefab's editable subtree as JSON, then load into our scene
  // as the only child of the wrapper root.
  std::string objJson = asset->prefab->obj.serialize().dump();
  scene.loadFromObjectJSON(objJson);
  savedJSON = scene.serializeRootChild();

  // Snapshot class variables into a local working copy. The Variables tab
  // mutates this list; saveToDisk writes it back onto asset->prefab.
  variables = asset->prefab->variables;
  savedVarsJSON = varsToJSONString(variables);

  // Pre-select the prefab root so the inspector shows something on open and
  // pressing the camera-focus shortcut frames the prefab subtree.
  auto &root = scene.getRootObject();
  if (!root.children.empty()) {
    selection.set(root.children.front()->uuid);
  }
}

void Editor::PrefabEditor::saveToDisk()
{
  if (filePath.empty() || !ctx.project) return;

  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB || !asset->prefab) return;

  // Pull the edited subtree out of the in-memory scene; rewrite the prefab's
  // canonical Object so any open instance inspectors see the new content.
  std::string subtreeJson = scene.serializeRootChild();
  auto subtreeDoc = nlohmann::json::parse(subtreeJson, nullptr, false);
  if (subtreeDoc.is_object()) {
    asset->prefab->obj = Project::Object{};
    asset->prefab->obj.deserialize(nullptr, subtreeDoc);
  }

  // Push the editor's working variables list onto the prefab before serialize.
  asset->prefab->variables = variables;

  // For variants, recompute the patch (diff against parent) before persisting
  // so the on-disk file stores deltas, not the resolved tree.
  if (asset->prefab->isVariant()) {
    auto parent = ctx.project->getAssets().getPrefabByUUID(
      asset->prefab->uuidParentPrefab.value
    );
    if (parent) {
      asset->prefab->rebuildPatchFromCurrent(*parent);
    } else {
      Utils::Logger::log(
        "Variant prefab " + filePath + " parent missing on save — skipping patch rebuild.",
        Utils::Logger::LEVEL_ERROR
      );
    }
  }

  // Persist via the canonical prefab serializer (writes uuid + obj or uuid + patch).
  Utils::FS::saveTextFile(filePath, asset->prefab->serialize());
  savedJSON = subtreeJson;
  savedVarsJSON = varsToJSONString(variables);
  history.markSaved();
  Utils::Logger::log("Saved prefab: " + filePath);

  // Re-resolve descendant variants whose parent chain runs through this
  // prefab; they may have referenced fields that just shifted. The pass is
  // idempotent and cheap, so we re-run the whole thing.
  auto &assets = ctx.project->getAssets();
  auto descendants = assets.getPrefabDescendants(asset->prefab->uuid.value);
  assets.resolvePrefabVariants();

  // Refresh instances of this prefab + every descendant variant in the
  // active scene, so structural edits propagate without a project reload.
  if (auto *active = ctx.project->getScenes().getLoadedScene()) {
    active->refreshPrefabInstances(asset->prefab->uuid.value);
    for (auto descUUID : descendants) {
      active->refreshPrefabInstances(descUUID);
    }
  }
}

bool Editor::PrefabEditor::isDirty() const
{
  if (scene.serializeRootChild() != savedJSON) return true;
  return varsToJSONString(variables) != savedVarsJSON;
}

std::string Editor::PrefabEditor::getName() const
{
  if (!ctx.project) return {};
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  return asset ? asset->name : std::string{};
}

bool Editor::PrefabEditor::draw(ImGuiID defDockId)
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::PREFAB) return false;

  std::string baseTitle = std::string{ICON_MDI_PACKAGE_VARIANT_CLOSED " "}
    + asset->name + (isDirty() ? " *" : "");
  // ID suffix bumped (was ###PrefabEditor_) so stale imgui.ini entries from
  // the old auto-dock-into-3D-Viewport behavior don't override our new
  // spawn-as-its-own-OS-window default.
  winName = baseTitle + "###PrefabEditorWin_" + std::to_string(assetUUID);

  // Spawn outside the main viewport's right edge so ImGui's multi-viewport
  // backend gives this editor its own OS window (Unreal-style asset editor).
  // FirstUseEver lets the user re-dock or move it later and have that stick.
  auto *mvp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
    {mvp->Pos.x + mvp->Size.x + 20.0f, mvp->Pos.y + 60.0f},
    ImGuiCond_FirstUseEver
  );
  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  // Don't gate the body on Begin's return value — matches ModelEditor's
  // pattern. When Begin returns false (window hidden after X click), ImGui
  // renders the body widgets to a hidden draw list, but our internal C++
  // (viewport.draw) still runs every frame the editor is in the map. That
  // keeps GPU resource lifetime predictable: the framebuffer is written to
  // every frame up to and including the close frame, matching the
  // defer-by-one destruction timing that ModelEditor / AssetPreviewViewport
  // rely on. Gating the body would skip GPU writes on the close frame, which
  // sounds safer but actually leaves SDL_GPU's in-flight texture
  // tracking inconsistent with our defer cadence.
  ImGui::Begin(winName.c_str(), &isOpen,
        isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0);

  // EditScope binds this editor's history+scene+selection so that any
  // markChanged() call inside the nested SceneGraph / ObjectInspector routes
  // here, not to the main scene's history.
  Editor::UndoRedo::EditScope scope(history, scene, selection);

  // Toolbar
  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) saveToDisk();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", filePath.c_str());

  // Ctrl+S → save (only when this window has focus).
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      saveToDisk();
    }
    // Undo/Redo route to this editor's history via the active EditScope.
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      history.undo();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
      history.redo();
    }
  }

  // Three-pane body, modeled after Unreal's Blueprint Editor:
  //   [hierarchy + variables + functions] | [3D viewport] | [details]
  // The left pane stacks hierarchy / variables / functions sections like
  // Unreal's "My Blueprint" panel. Two horizontal splitters separate the
  // three columns; pane widths are stored as fractions of avail so window
  // resizes keep the ratio.
  float avail = ImGui::GetContentRegionAvail().x;
  float leftW = ImClamp(avail * leftSplitFrac,
    MIN_PANE_WIDTH, avail - 2.0f * MIN_PANE_WIDTH - 2.0f * SPLITTER_WIDTH);
  float rightW = ImClamp(avail * rightSplitFrac,
    MIN_PANE_WIDTH, avail - leftW - MIN_PANE_WIDTH - 2.0f * SPLITTER_WIDTH);

  auto drawSplitter = [](const char* id, float &fracTarget, float currentW, float avail,
                         float minThisPane, float minOtherPanes) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
    ImGui::Button(id, ImVec2(SPLITTER_WIDTH, -1));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
      float delta = ImGui::GetIO().MouseDelta.x;
      float newW = currentW + delta;
      float minFrac = minThisPane / avail;
      float maxFrac = (avail - minOtherPanes) / avail;
      fracTarget = ImClamp(newW / avail, minFrac, maxFrac);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
  };

  ImGui::BeginChild("##LeftPane", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
    drawLeftPane();
  ImGui::EndChild();

  ImGui::SameLine();
  drawSplitter("##SplitterL", leftSplitFrac, leftW, avail,
               MIN_PANE_WIDTH, MIN_PANE_WIDTH + rightW + 2.0f * SPLITTER_WIDTH);
  ImGui::SameLine();

  ImGui::BeginChild("##ViewportPane", ImVec2(avail - leftW - rightW - 2.0f * SPLITTER_WIDTH, 0),
    ImGuiChildFlags_Borders);
    viewport.draw();
  ImGui::EndChild();

  ImGui::SameLine();
  drawSplitter("##SplitterR", rightSplitFrac, rightW, avail,
               MIN_PANE_WIDTH, leftW + 2.0f * SPLITTER_WIDTH + MIN_PANE_WIDTH);
  ImGui::SameLine();

  ImGui::BeginChild("##InspectorPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
    inspector.draw(scene, selection);
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::PrefabEditor::drawLeftPane()
{
  // Mirrors UE5's Blueprint editor: the left side is split vertically into
  // a Components panel on top (the prefab's object tree) and a "My Prefab"
  // panel on the bottom (variables + functions). Both have their own
  // header bar and border so the two roles read as distinct surfaces.
  float availH = ImGui::GetContentRegionAvail().y;
  float topH = ImClamp(availH * leftVerticalSplitFrac,
                       80.0f, availH - 80.0f - SPLITTER_WIDTH);

  // ---- Components (hierarchy) ----
  ImGui::BeginChild("##ComponentsPanel", ImVec2(0, topH),
    ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s  Components", ICON_MDI_FILE_TREE);
    ImGui::Separator();
    graph.draw(scene, selection);
  ImGui::EndChild();

  // ---- Horizontal splitter ----
  ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
  ImGui::Button("##LeftVSplitter", ImVec2(-1, SPLITTER_WIDTH));
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemActive()) {
    float delta = ImGui::GetIO().MouseDelta.y;
    float newH = topH + delta;
    float minFrac = 80.0f / availH;
    float maxFrac = (availH - 80.0f - SPLITTER_WIDTH) / availH;
    leftVerticalSplitFrac = ImClamp(newH / availH, minFrac, maxFrac);
  }
  if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }

  // ---- My Prefab (variables + functions) ----
  ImGui::BeginChild("##MyPrefabPanel", ImVec2(0, 0),
    ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s  My Prefab", ICON_MDI_PACKAGE_VARIANT_CLOSED);
    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_MDI_GRAPH " Graphs",
          ImGuiTreeNodeFlags_DefaultOpen)) {
      drawGraphsPanel();
    }
    if (ImGui::CollapsingHeader(ICON_MDI_VARIABLE " Variables",
          ImGuiTreeNodeFlags_DefaultOpen)) {
      drawVariablesPanel();
    }
    if (ImGui::CollapsingHeader(ICON_MDI_FUNCTION " Functions",
          ImGuiTreeNodeFlags_DefaultOpen)) {
      drawFunctionsPanel();
    }
  ImGui::EndChild();
}

void Editor::PrefabEditor::drawVariablesPanel()
{
  // Header: add a new variable. Defaults to INT, name is auto-generated.
  if (ImGui::Button(ICON_MDI_PLUS " Add Variable")) {
    Project::PrefabVarDef v{};
    v.uuid = Utils::Hash::sha256_64bit(
      std::to_string(rand()) + std::to_string(variables.size())
    );
    v.name = "NewVar" + std::to_string(variables.size());
    v.kind = Project::PrefabVarKind::INT;
    v.defaultValue.set<int32_t>(0);
    variables.push_back(std::move(v));
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu)", variables.size());
  ImGui::Separator();

  if (variables.empty()) {
    ImGui::TextDisabled("No variables.");
    return;
  }

  static const char* kindNames[] = {
    "Int", "Float", "Bool", "Vec3", "Quat",
    "Object Ref", "Prefab Ref", "Asset Ref",
  };

  if (!ImGui::BeginTable("##Vars", 4,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
    return;
  }
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.25f);
  ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.20f);
  ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.45f);
  ImGui::TableSetupColumn("##Del", ImGuiTableColumnFlags_WidthFixed, 28.0f);
  ImGui::TableHeadersRow();

  int delIdx = -1;
  for (size_t i = 0; i < variables.size(); ++i) {
    auto &v = variables[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::TableNextRow();

    // Name
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    char nameBuf[128]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", v.name.c_str());
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
      v.name = nameBuf;
    }

    // Type
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    int kindIdx = static_cast<int>(v.kind);
    if (ImGui::Combo("##kind", &kindIdx, kindNames, IM_ARRAYSIZE(kindNames))) {
      v.kind = static_cast<Project::PrefabVarKind>(kindIdx);
      // Reset the default to a sane zero of the new type — switching kinds
      // would otherwise leave a GenericValue payload typed for the old kind.
      v.defaultValue = GenericValue{};
      v.typeArg = 0;
      switch (v.kind) {
        case Project::PrefabVarKind::INT:        v.defaultValue.set<int32_t>(0); break;
        case Project::PrefabVarKind::FLOAT:      v.defaultValue.set<float>(0.0f); break;
        case Project::PrefabVarKind::BOOL:       v.defaultValue.set<bool>(false); break;
        case Project::PrefabVarKind::VEC3:       v.defaultValue.set<glm::vec3>({0,0,0}); break;
        case Project::PrefabVarKind::QUAT:       v.defaultValue.set<glm::quat>(glm::quat{1,0,0,0}); break;
        case Project::PrefabVarKind::OBJECT_REF:
        case Project::PrefabVarKind::PREFAB_REF:
        case Project::PrefabVarKind::ASSET_REF:  v.defaultValue.set<uint64_t>(0); break;
      }
    }

    // Default value widget (per-type)
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    switch (v.kind) {
      case Project::PrefabVarKind::INT: {
        int val = v.defaultValue.get<int32_t>();
        if (ImGui::DragInt("##def", &val)) v.defaultValue.set<int32_t>(val);
        break;
      }
      case Project::PrefabVarKind::FLOAT: {
        float val = v.defaultValue.get<float>();
        if (ImGui::DragFloat("##def", &val, 0.01f)) v.defaultValue.set<float>(val);
        break;
      }
      case Project::PrefabVarKind::BOOL: {
        bool val = v.defaultValue.get<bool>();
        if (ImGui::Checkbox("##def", &val)) v.defaultValue.set<bool>(val);
        break;
      }
      case Project::PrefabVarKind::VEC3: {
        glm::vec3 val = v.defaultValue.get<glm::vec3>();
        if (ImGui::DragFloat3("##def", &val.x, 0.01f)) v.defaultValue.set<glm::vec3>(val);
        break;
      }
      case Project::PrefabVarKind::QUAT: {
        glm::quat q = v.defaultValue.get<glm::quat>();
        float xyzw[4]{q.x, q.y, q.z, q.w};
        if (ImGui::DragFloat4("##def", xyzw, 0.01f)) {
          v.defaultValue.set<glm::quat>(glm::quat{xyzw[3], xyzw[0], xyzw[1], xyzw[2]});
        }
        break;
      }
      case Project::PrefabVarKind::OBJECT_REF: {
        // Per-instance only — class-level default is always null. Object refs
        // resolve at scene-graph time, not at prefab-edit time.
        ImGui::TextDisabled("(null — set per instance)");
        break;
      }
      case Project::PrefabVarKind::PREFAB_REF: {
        // typeArg pins the target prefab type. Instances will be picked from
        // prefabs of that type (or its descendants) via a scene picker.
        std::string label = "(none)";
        if (ctx.project) {
          auto *e = ctx.project->getAssets().getEntryByUUID(v.typeArg);
          if (e) label = e->name;
        }
        if (ImGui::BeginCombo("##def", label.c_str())) {
          if (ctx.project) {
            for (const auto &e : ctx.project->getAssets().getTypeEntries(Project::FileType::PREFAB)) {
              uint64_t entryUUID = e.getUUID();
              if (entryUUID == assetUUID) continue; // can't ref self
              bool sel = (entryUUID == v.typeArg);
              std::string entryLabel = e.name + "##" + std::to_string(entryUUID);
              if (ImGui::Selectable(entryLabel.c_str(), sel)) {
                v.typeArg = entryUUID;
              }
            }
          }
          ImGui::EndCombo();
        }
        break;
      }
      case Project::PrefabVarKind::ASSET_REF: {
        // Asset-ref variables are reserved for a follow-up phase; the on-disk
        // schema accepts them but the inspector picker isn't wired yet.
        ImGui::TextDisabled("(asset ref - TODO)");
        break;
      }
    }

    // Delete row
    ImGui::TableNextColumn();
    if (ImGui::SmallButton(ICON_MDI_DELETE)) {
      delIdx = static_cast<int>(i);
    }
    ImGui::PopID();
  }
  ImGui::EndTable();

  if (delIdx >= 0) {
    variables.erase(variables.begin() + delIdx);
  }
}

void Editor::PrefabEditor::drawGraphsPanel()
{
  // Every prefab carries a default EventGraph — the canvas where events
  // (OnReady, OnTick, OnHurt…) wire up to user functions. Double-click
  // (or single-click — Selectable handles both) opens the event graph
  // window for this prefab; the window persists its state back into the
  // prefab on save.
  if (ImGui::Selectable(ICON_MDI_GRAPH " EventGraph", false,
        ImGuiSelectableFlags_AllowDoubleClick)) {
    if (ctx.editorScene) ctx.editorScene->openPrefabEventGraphEditor(assetUUID);
  }
}

void Editor::PrefabEditor::drawFunctionsPanel()
{
  if (!ctx.project) return;

  // Re-scan each draw — the .h is edited in an external editor, so changes
  // there should appear immediately. Scans are I/O-light (one regex pass
  // over a small header).
  const std::string prefabName = getName();
  functions = Project::scanPrefabFunctions(ctx.project->getPath(), prefabName);

  // "+ Add" mirrors UE5's My Blueprint panel header. Picks a default name
  // that doesn't collide with an existing function so repeated clicks
  // produce NewFunction, NewFunction_2, NewFunction_3...
  if (ImGui::SmallButton(ICON_MDI_PLUS " Add")) {
    auto isTaken = [&](const std::string &n) {
      for (const auto &f : functions) if (f.name == n) return true;
      return false;
    };
    std::string fnName = "NewFunction";
    if (isTaken(fnName)) {
      for (int i = 2; i < 1000; ++i) {
        std::string candidate = "NewFunction_" + std::to_string(i);
        if (!isTaken(candidate)) { fnName = candidate; break; }
      }
    }
    Project::addPrefabFunction(ctx.project->getPath(), prefabName, fnName);
    // The next draw will rescan and pick the new entry up automatically.
  }

  if (functions.empty()) {
    ImGui::TextDisabled("No functions.");
    return;
  }

  // Compact class-method list: name first, full signature underneath in
  // dimmed text. Matches the visual rhythm of Unreal's My Blueprint pane
  // where each entry is a single clickable row.
  for (const auto &f : functions) {
    ImGui::PushID(f.name.c_str());
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextUnformatted(f.name.c_str());
    if (!f.returnType.empty() || !f.params.empty()) {
      ImGui::Indent();
      ImGui::TextDisabled("%s(%s)",
        f.returnType.c_str(),
        f.params.empty() ? "" : f.params.c_str()
      );
      ImGui::Unindent();
    }
    ImGui::PopID();
  }
}

void Editor::PrefabEditor::focus() const
{
  ImGui::SetWindowFocus(("###PrefabEditorWin_" + std::to_string(assetUUID)).c_str());
}
