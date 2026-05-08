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

  // Force ImGui to give this editor its own OS-level platform window
  // (Unreal-style asset editor) instead of merging it back into the main
  // viewport. NoAutoMerge -> always own viewport. Clearing NoDecoration
  // restores the OS title bar, resize handles, and maximize button —
  // ImGui defaults secondary viewports to borderless via
  // ConfigViewportsNoDecoration=true, which is fine for tooltips but wrong
  // for asset editors the user actually drags around between monitors.
  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  auto *mvp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
    {
      mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
      mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
    },
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
    drawDetailsPanel();
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::PrefabEditor::clearMyPrefabSelection()
{
  detailsKind = DetailsKind::OBJECT;
  detailsVarIdx = -1;
  detailsFuncName.clear();
  renameBuffer.clear();
}

void Editor::PrefabEditor::drawDetailsPanel()
{
  // UE-style mutual-exclusion: clicking an Object node in the Components
  // tree clobbers the my-prefab selection so Details snaps back to the
  // object inspector. Detect by watching selection.primary() for changes
  // — if it just became non-zero this frame, the user just clicked.
  uint32_t curSel = selection.primary();
  if (curSel != 0 && curSel != lastSelectionPrimary) {
    detailsKind = DetailsKind::OBJECT;
    detailsVarIdx = -1;
    detailsFuncName.clear();
  }
  lastSelectionPrimary = curSel;

  switch (detailsKind) {
    case DetailsKind::VARIABLE: drawVariableDetails(); break;
    case DetailsKind::FUNCTION: drawFunctionDetails(); break;
    case DetailsKind::OBJECT:
    default:                    inspector.draw(scene, selection); break;
  }
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
  // Clicking selects the new variable so the user can immediately edit
  // it in the details panel — UE Blueprint pattern.
  if (ImGui::SmallButton(ICON_MDI_PLUS " Add")) {
    Project::PrefabVarDef v{};
    v.uuid = Utils::Hash::sha256_64bit(
      std::to_string(rand()) + std::to_string(variables.size())
    );
    v.name = "NewVar_" + std::to_string(variables.size() + 1);
    v.kind = Project::PrefabVarKind::INT;
    v.defaultValue.set<int32_t>(0);
    variables.push_back(std::move(v));
    detailsKind = DetailsKind::VARIABLE;
    detailsVarIdx = static_cast<int>(variables.size()) - 1;
    detailsFuncName.clear();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu)", variables.size());

  if (variables.empty()) {
    ImGui::TextDisabled("(none)");
    return;
  }

  // Selectable rows — single-line "icon + name + type-tag". Clicking the
  // row selects the variable for the details panel; no inline editing in
  // the panel itself (kept minimal, like UE5 My Blueprint).
  static const char* kindShort[] = {
    "int", "float", "bool", "vec3", "quat", "obj", "prefab", "asset",
  };
  for (size_t i = 0; i < variables.size(); ++i) {
    const auto &v = variables[i];
    ImGui::PushID(static_cast<int>(i));
    bool sel = (detailsKind == DetailsKind::VARIABLE
                && detailsVarIdx == static_cast<int>(i));
    std::string label = std::string{ICON_MDI_VARIABLE " "} + v.name;
    if (ImGui::Selectable(label.c_str(), sel,
          ImGuiSelectableFlags_AllowOverlap)) {
      detailsKind = DetailsKind::VARIABLE;
      detailsVarIdx = static_cast<int>(i);
      detailsFuncName.clear();
    }
    // Right-aligned type tag.
    ImGui::SameLine(ImGui::GetContentRegionAvail().x
                    + ImGui::GetCursorPosX() - 60.0f);
    int k = static_cast<int>(v.kind);
    if (k < 0 || k >= IM_ARRAYSIZE(kindShort)) k = 0;
    ImGui::TextDisabled("%s", kindShort[k]);
    ImGui::PopID();
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
    // Select the new function so the user can see its details immediately.
    detailsKind = DetailsKind::FUNCTION;
    detailsFuncName = fnName;
    detailsVarIdx = -1;
    renameBuffer = fnName;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu)", functions.size());

  if (functions.empty()) {
    ImGui::TextDisabled("(none)");
    return;
  }

  // Selectable row per function — UE-Blueprint style. Single click selects
  // (details panel populates), double click opens the .cpp body in the
  // editor's CodeEditor, right click brings up rename / delete / open.
  for (const auto &f : functions) {
    ImGui::PushID(f.name.c_str());
    bool sel = (detailsKind == DetailsKind::FUNCTION
                && detailsFuncName == f.name);
    std::string label = std::string{ICON_MDI_FUNCTION " "} + f.name;
    if (ImGui::Selectable(label.c_str(), sel,
          ImGuiSelectableFlags_AllowDoubleClick)) {
      detailsKind = DetailsKind::FUNCTION;
      detailsFuncName = f.name;
      detailsVarIdx = -1;
      renameBuffer = f.name;
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
          && ctx.project && ctx.editorScene) {
        // Open the prefab's .cpp source in CodeEditor.
        std::string cppPath = ctx.project->getPath() + "/src/user/"
                            + prefabName + ".cpp";
        auto *entry = ctx.project->getAssets().getByPath(cppPath);
        if (entry) ctx.editorScene->openCodeEditor(entry->getUUID());
      }
    }
    ImGui::PopID();
  }
}

void Editor::PrefabEditor::focus() const
{
  ImGui::SetWindowFocus(("###PrefabEditorWin_" + std::to_string(assetUUID)).c_str());
}
