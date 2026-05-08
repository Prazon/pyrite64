// added by SPBF64 fork
#include "prefabEditor.h"

#include <algorithm>
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
  // what's on every node without having to click through them. prefabRootMode
  // skips the synthetic Scene wrapper so the prefab root reads as the
  // topmost item — and it gets the prefab icon as its badge.
  graph.showComponentsInline = true;
  graph.prefabRootMode       = true;
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
  // Stable ImGui ID via ###suffix so renaming the asset doesn't lose
  // saved dock state, and so legacy imgui.ini entries (no ### before this)
  // don't override our default tab placement next to Scene Editor.
  winName = baseTitle + "###PrefabEditorWin_" + std::to_string(assetUUID);

  // Dock as a sibling tab of "Scene Editor" in the outer top region on
  // first open — so focusing this editor swaps the upper area instead of
  // squeezing into the same panel as the 3D-Viewport. NoAutoMerge +
  // cleared NoDecoration only matter when the user drags the tab out into
  // its own OS window: full native chrome (title bar / resize / maximize).
  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  if (defDockId) ImGui::SetNextWindowDockID(defDockId, ImGuiCond_FirstUseEver);

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
  // Host window: contains a toolbar + a DockSpace. The four dockable panels
  // (Components / My Prefab / Viewport / Details) live as their own ImGui
  // windows that target this dockspace — UE-Blueprint behaviour where the
  // user can rearrange tabs, drag panels into their own OS windows, etc.
  ImGui::Begin(winName.c_str(), &isOpen,
        ImGuiWindowFlags_NoCollapse
        | (isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0));

  Editor::UndoRedo::EditScope scope(history, scene, selection);

  // Toolbar.
  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) saveToDisk();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", filePath.c_str());

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveToDisk();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) history.undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) history.redo();
  }

  // Per-instance dock-space — keyed by asset UUID so multiple open prefab
  // editors don't collide on layout state. First-time init builds the
  // default UE-style layout; afterwards ImGui's own .ini persistence keeps
  // user rearrangements stuck.
  const std::string uuidStr = std::to_string(assetUUID);
  const std::string dockId = "PrefabDock_" + uuidStr;
  ImGuiID dockspaceID = ImGui::GetID(dockId.c_str());
  bool firstBuild = (ImGui::DockBuilderGetNode(dockspaceID) == nullptr);
  ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), 0);

  // Window names — suffixed with the asset UUID so they're unique across
  // open editors. ID-string trick lets us change the visible title without
  // losing dock state.
  const std::string winComp = std::string{ICON_MDI_FILE_TREE              "  Components##PrefabComp_"} + uuidStr;
  const std::string winMyP  = std::string{ICON_MDI_PACKAGE_VARIANT_CLOSED "  My Prefab##PrefabMyP_"}  + uuidStr;
  const std::string winVP   = std::string{ICON_MDI_VIEW_QUILT             "  Viewport##PrefabVP_"}    + uuidStr;
  const std::string winDet  = std::string{ICON_MDI_INFORMATION            "  Details##PrefabDet_"}    + uuidStr;

  if (firstBuild) {
    ImGui::DockBuilderRemoveNode(dockspaceID);
    ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetContentRegionAvail());

    ImGuiID center = dockspaceID;
    ImGuiID left, right, leftBottom;
    left  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.22f, nullptr, &center);
    right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
    leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.50f, nullptr, &left);

    ImGui::DockBuilderDockWindow(winComp.c_str(), left);
    ImGui::DockBuilderDockWindow(winMyP.c_str(),  leftBottom);
    ImGui::DockBuilderDockWindow(winVP.c_str(),   center);
    ImGui::DockBuilderDockWindow(winDet.c_str(),  right);
    ImGui::DockBuilderFinish(dockspaceID);
    // Remember the viewport's node so we can dock EventGraph + function
    // source tabs next to it from openPrefabEventGraphEditor / openCodeEditorByPath.
    viewportDockNodeID = center;
  }

  ImGui::End(); // host

  // Each panel is a standalone ImGui window. Pass nullptr for the open-bool
  // so they can't be closed individually — they're permanent dock targets
  // for this editor instance.
  ImGui::Begin(winComp.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    graph.selectedComponentUUID = detailsCompUUID;
    graph.draw(scene, selection);

    // Consume the SceneGraph's per-frame outputs.
    if (graph.pendingPromoteToRoot != 0) {
      Editor::UndoRedo::getHistory().markChanged("Make Root");
      scene.promoteToPrefabRoot(graph.pendingPromoteToRoot);
      graph.pendingPromoteToRoot = 0;
    }
    if (graph.pendingComponentUUID != 0) {
      detailsCompObjUUID = graph.pendingComponentObjUUID;
      detailsCompUUID    = graph.pendingComponentUUID;
      detailsKind        = DetailsKind::COMPONENT;
      detailsVarIdx      = -1;
      detailsFuncName.clear();
      graph.pendingComponentUUID    = 0;
      graph.pendingComponentObjUUID = 0;
    }
  ImGui::End();

  ImGui::Begin(winMyP.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    if (ImGui::CollapsingHeader(ICON_MDI_GRAPH " Graphs",
          ImGuiTreeNodeFlags_DefaultOpen)) drawGraphsPanel();
    if (ImGui::CollapsingHeader(ICON_MDI_VARIABLE " Variables",
          ImGuiTreeNodeFlags_DefaultOpen)) drawVariablesPanel();
    if (ImGui::CollapsingHeader(ICON_MDI_FUNCTION " Functions",
          ImGuiTreeNodeFlags_DefaultOpen)) drawFunctionsPanel();
  ImGui::End();

  ImGui::Begin(winVP.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    // Refresh the viewport's dock node each frame so user-driven layout
    // changes (drag-out, re-dock, OS-window split) keep the EventGraph /
    // function-source spawn target in sync with where the viewport actually
    // lives now. firstBuild seeds this; this line keeps it current.
    if (auto *node = ImGui::GetWindowDockNode()) {
      viewportDockNodeID = node->ID;
    }
    viewport.draw();
  ImGui::End();

  ImGui::Begin(winDet.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
    drawDetailsPanel();
  ImGui::End();

  return isOpen;
}

void Editor::PrefabEditor::clearMyPrefabSelection()
{
  detailsKind = DetailsKind::OBJECT;
  detailsVarIdx = -1;
  detailsFuncName.clear();
  detailsCompUUID = 0;
  detailsCompObjUUID = 0;
  renameBuffer.clear();
}

void Editor::PrefabEditor::drawDetailsPanel()
{
  // UE-style mutual-exclusion: clicking an Object node in the Components
  // tree clobbers the my-prefab selection (variables / functions) so
  // Details snaps back to the object inspector. Detect by watching
  // selection.primary() for changes — if it just became non-zero this
  // frame, the user just clicked. Component-leaf clicks set both the
  // primary selection AND COMPONENT mode in the same frame, so we
  // preserve COMPONENT mode in that case.
  uint32_t curSel = selection.primary();
  if (curSel != 0 && curSel != lastSelectionPrimary
      && detailsKind != DetailsKind::COMPONENT) {
    detailsKind = DetailsKind::OBJECT;
    detailsVarIdx = -1;
    detailsFuncName.clear();
    detailsCompUUID = 0;
    detailsCompObjUUID = 0;
  }
  lastSelectionPrimary = curSel;

  switch (detailsKind) {
    case DetailsKind::VARIABLE:  drawVariableDetails();  break;
    case DetailsKind::FUNCTION:  drawFunctionDetails();  break;
    case DetailsKind::COMPONENT: drawComponentDetails(); break;
    case DetailsKind::OBJECT:
    default:                     inspector.draw(scene, selection); break;
  }
}

// drawLeftPane is unused after the dockspace restructure (the panels live
// as standalone ImGui windows now). Definition kept compiled-out so the
// header declaration doesn't generate a linker stub at -Os; no callers.
#if 0
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
#endif // drawLeftPane (unused)

void Editor::PrefabEditor::drawVariablesPanel()
{
  // Scope all IDs in this panel under "vars" so the +Add button doesn't
  // collide with the identical button in drawFunctionsPanel (both render
  // into the same My-Prefab window — ImGui hashes button labels and would
  // otherwise emit "two visible items with conflicting ID" warnings).
  ImGui::PushID("vars");
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
    ImGui::PopID(); // "vars"
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
  ImGui::PopID(); // "vars"
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
    if (ctx.editorScene) {
      // Dock the graph into our viewport node so it opens as a sibling tab
      // of the prefab viewport (UE-Blueprint feel: the graph and the
      // 3D-preview share the central area).
      ctx.editorScene->openPrefabEventGraphEditor(assetUUID, viewportDockNodeID);
    }
  }
}

void Editor::PrefabEditor::drawFunctionsPanel()
{
  if (!ctx.project) return;

  // ID-scope this panel separately from drawVariablesPanel so the +Add
  // button (identical label/glyph) doesn't collide with the variables
  // version inside the shared My-Prefab window.
  ImGui::PushID("funcs");

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
    ImGui::PopID(); // "funcs"
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
        // Open the prefab's .cpp source via path-based opener — these files
        // live at <project>/src/user/<name>.cpp and use `namespace User::`,
        // which AssetManager::buildCodeEntry doesn't dispatch on, so they
        // never appear as AssetManagerEntries. Pass viewportDockNodeID so
        // the new tab lands as a sibling of this prefab editor's viewport.
        std::string cppPath = ctx.project->getPath() + "/src/user/"
                            + prefabName + ".cpp";
        ctx.editorScene->openCodeEditorByPath(cppPath, viewportDockNodeID);
        // Track ownership so EditorScene can close this tab when the
        // prefab editor itself is dismissed. Same hash openCodeEditorByPath
        // uses to key its codeEditors map.
        uint64_t synth = Utils::Hash::sha256_64bit(cppPath);
        if (std::find(ownedCodeEditorUUIDs.begin(),
                      ownedCodeEditorUUIDs.end(), synth)
              == ownedCodeEditorUUIDs.end()) {
          ownedCodeEditorUUIDs.push_back(synth);
        }
      }
    }
    ImGui::PopID();
  }
  ImGui::PopID(); // "funcs"
}

void Editor::PrefabEditor::drawVariableDetails()
{
  if (detailsVarIdx < 0 || detailsVarIdx >= (int)variables.size()) {
    clearMyPrefabSelection();
    inspector.draw(scene, selection);
    return;
  }
  auto &v = variables[detailsVarIdx];

  ImGui::TextDisabled("%s  Variable", ICON_MDI_VARIABLE);
  ImGui::Separator();

  if (ImTable::start("VarDetails")) {
    ImTable::add("Name");
    ImGui::SetNextItemWidth(-1);
    char nameBuf[128]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", v.name.c_str());
    if (ImGui::InputText("##varname", nameBuf, sizeof(nameBuf))) {
      v.name = nameBuf;
    }

    ImTable::add("Type");
    ImGui::SetNextItemWidth(-1);
    static const char* kindNames[] = {
      "Int", "Float", "Bool", "Vec3", "Quat",
      "Object Ref", "Prefab Ref", "Asset Ref",
    };
    int kindIdx = static_cast<int>(v.kind);
    if (ImGui::Combo("##varkind", &kindIdx, kindNames, IM_ARRAYSIZE(kindNames))) {
      v.kind = static_cast<Project::PrefabVarKind>(kindIdx);
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

    if (v.kind == Project::PrefabVarKind::PREFAB_REF) {
      ImTable::add("Target Prefab");
      ImGui::SetNextItemWidth(-1);
      std::string label = "(none)";
      if (ctx.project) {
        auto *e = ctx.project->getAssets().getEntryByUUID(v.typeArg);
        if (e) label = e->name;
      }
      if (ImGui::BeginCombo("##targ", label.c_str())) {
        if (ctx.project) {
          for (const auto &e : ctx.project->getAssets().getTypeEntries(Project::FileType::PREFAB)) {
            uint64_t entryUUID = e.getUUID();
            if (entryUUID == assetUUID) continue;
            bool sel = (entryUUID == v.typeArg);
            std::string entryLabel = e.name + "##" + std::to_string(entryUUID);
            if (ImGui::Selectable(entryLabel.c_str(), sel)) v.typeArg = entryUUID;
          }
        }
        ImGui::EndCombo();
      }
    }

    ImTable::add("Default");
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
      case Project::PrefabVarKind::OBJECT_REF: ImGui::TextDisabled("(null — set per instance)"); break;
      case Project::PrefabVarKind::PREFAB_REF: ImGui::TextDisabled("(null — set per instance)"); break;
      case Project::PrefabVarKind::ASSET_REF:  ImGui::TextDisabled("(asset ref - TODO)"); break;
    }

    ImTable::end();
  }

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button(ICON_MDI_DELETE " Delete Variable")) {
    variables.erase(variables.begin() + detailsVarIdx);
    clearMyPrefabSelection();
  }
}

void Editor::PrefabEditor::drawFunctionDetails()
{
  if (detailsFuncName.empty() || !ctx.project) {
    clearMyPrefabSelection();
    inspector.draw(scene, selection);
    return;
  }

  // Find the descriptor in the cached scan list. If it's no longer there
  // (e.g. the .h was edited externally), fall back to a minimal view that
  // still lets the user open the .cpp or rename.
  const Project::PrefabFunctionDesc *fd = nullptr;
  for (const auto &f : functions) {
    if (f.name == detailsFuncName) { fd = &f; break; }
  }

  ImGui::TextDisabled("%s  Function", ICON_MDI_FUNCTION);
  ImGui::Separator();

  if (ImTable::start("FuncDetails")) {
    ImTable::add("Name");
    ImGui::SetNextItemWidth(-1);
    char nameBuf[128]{};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s",
      renameBuffer.empty() ? detailsFuncName.c_str() : renameBuffer.c_str());
    if (ImGui::InputText("##fnname", nameBuf, sizeof(nameBuf))) {
      renameBuffer = nameBuf;
    }
    bool canCommit = !renameBuffer.empty() && renameBuffer != detailsFuncName;
    if (canCommit) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Apply")) {
        if (Project::renamePrefabFunction(
              ctx.project->getPath(), getName(), detailsFuncName, renameBuffer)) {
          detailsFuncName = renameBuffer;
        }
      }
    }

    ImTable::add("Returns");
    ImGui::TextUnformatted(fd ? fd->returnType.c_str() : "(unknown)");

    ImTable::add("Parameters");
    if (fd && !fd->params.empty()) ImGui::TextWrapped("%s", fd->params.c_str());
    else ImGui::TextDisabled("(none)");

    if (fd) {
      ImTable::add("Source");
      ImGui::TextDisabled("src/user/%s.h:%d", getName().c_str(), fd->line);
    }

    ImTable::end();
  }

  ImGui::Spacing();
  ImGui::Separator();
  // Editing source is initiated by double-clicking the function row in the
  // Functions panel — the redundant "Open in Editor" button used to live
  // here but was removed: the row gesture is the canonical entry point and
  // it docks the source tab next to the prefab viewport, which the button
  // path didn't.
  if (ImGui::Button(ICON_MDI_DELETE " Delete")) {
    if (Project::removePrefabFunction(
          ctx.project->getPath(), getName(), detailsFuncName)) {
      clearMyPrefabSelection();
    }
  }
}

void Editor::PrefabEditor::drawComponentDetails()
{
  // Render only the single component the user clicked on in the hierarchy.
  // Defers to the existing Component::TABLE funcDraw so per-type editors
  // stay identical to what the full ObjectInspector shows; we just isolate
  // the chosen entry. Selection mutual-exclusion: clicking an empty area
  // (which clears the primary selection) drops back to OBJECT in
  // drawDetailsPanel.
  auto obj = scene.getObjectByUUID(detailsCompObjUUID);
  if (!obj || detailsCompUUID == 0) {
    clearMyPrefabSelection();
    inspector.draw(scene, selection);
    return;
  }

  Project::Component::Entry* entry = nullptr;
  auto findIn = [&](std::vector<Project::Component::Entry> &list) {
    for (auto &e : list) {
      if (e.uuid == detailsCompUUID) { entry = &e; return; }
    }
  };
  findIn(obj->components);
  // Prefab-instance fall-through: also search the source prefab object's
  // components, since those are what the hierarchy shows for instances.
  if (!entry && obj->isPrefabInstance() && ctx.project) {
    auto prefab = ctx.project->getAssets().getPrefabByUUID(obj->uuidPrefab.value);
    if (prefab) findIn(prefab->obj.components);
  }

  if (!entry || entry->id < 0
      || (size_t)entry->id >= Project::Component::TABLE.size()) {
    clearMyPrefabSelection();
    inspector.draw(scene, selection);
    return;
  }

  const auto &def = Project::Component::TABLE[entry->id];
  ImGui::TextDisabled("%s  Component", def.icon ? def.icon : "");
  ImGui::SameLine();
  ImGui::TextUnformatted(entry->name.empty()
    ? std::string{def.name}.c_str()
    : entry->name.c_str());
  ImGui::Separator();
  def.funcDraw(*obj, *entry);
}

void Editor::PrefabEditor::focus() const
{
  ImGui::SetWindowFocus(("###PrefabEditorWin_" + std::to_string(assetUUID)).c_str());
}
