/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "materialEditor.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "ImNodeFlow.h"
#include "json.hpp"
#include "IconsMaterialDesignIcons.h"

#include "assetEditorDocking.h"
#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"
#include "../../../../project/materialGraph/nodes/baseNode.h"
#include "../../../../project/graph/nodeStyles.h"
#include "../../../imgui/helper.h"
#include "../assetInspector.h"
#include "../../editorScene.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{960, 600};

  void drawCreatePopup(Project::MaterialGraph::Graph &graph, ImFlow::Pin* pin)
  {
    ImGui::Text("Create");
    ImGui::Separator();
    auto &names = Project::MaterialGraph::Graph::getNodeNames();
    for (size_t i = 0; i < names.size(); ++i) {
      if (ImGui::Selectable(names[i].c_str())) {
        auto newPos = pin ? pin->getParent()->getPos() : ImVec2{0, 0};
        newPos.x += 150;
        auto node = graph.addNode(static_cast<uint32_t>(i), newPos);
        auto &ins = node->getIns();
        if (pin && !ins.empty()) ins[0]->createLink(pin);
        node->setPos(newPos);
        ImGui::CloseCurrentPopup();
      }
    }
  }
}

Editor::MaterialEditor::MaterialEditor(uint64_t materialAssetUUID)
  : assetUUID(materialAssetUUID)
{
  Project::Graph::initNodeStyles();

  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || !asset->materialAsset) return;

  const std::string &jsonState = asset->materialAsset->graphJSON;
  if (!jsonState.empty()) {
    graph.deserialize(jsonState);
  }
  // First-open seeding: drop a single Output sink so the canvas isn't
  // empty. Persist the seeded layout immediately so the .p64mat on disk
  // matches in-editor state next time it's opened.
  bool wasEmpty = graph.serialize().find("\"nodes\": []") != std::string::npos
              || graph.serialize().find("\"nodes\":[]") != std::string::npos;
  if (wasEmpty) {
    graph.seedDefaults();
    asset->materialAsset->graphJSON = graph.serialize();
    Utils::FS::saveTextFile(asset->path, asset->materialAsset->serialize());
  }
  savedState = graph.serialize();

  graph.graph.droppedLinkPopUpContent([this](ImFlow::Pin* pin) {
    drawCreatePopup(graph, pin);
  });
  graph.graph.rightClickPopUpContent([this](ImFlow::BaseNode* node) {
    if (node) {
      if (ImGui::Selectable(ICON_MDI_CONTENT_COPY " Duplicate")) {
        auto nodeP64 = (Project::MaterialGraph::Node::Base*)(node);
        ImVec2 newPos{
          node->getPos().x + node->getSize().x,
          node->getPos().y + 20.0f,
        };
        nlohmann::json jNode;
        nodeP64->serialize(jNode);
        auto newNode = graph.addNode(nodeP64->type, newPos);
        if (newNode) newNode->deserialize(jNode);
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::Selectable(ICON_MDI_TRASH_CAN_OUTLINE " Remove")) {
        node->destroy();
        ImGui::CloseCurrentPopup();
      }
    } else {
      drawCreatePopup(graph, nullptr);
    }
  });

  recompileCache();
}

void Editor::MaterialEditor::recompileCache()
{
  graph.compile(compiledCache);
}

std::string Editor::MaterialEditor::getName() const
{
  if (!ctx.project) return "Material";
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  return asset ? asset->name : std::string{"Material"};
}

bool Editor::MaterialEditor::isDirty() const
{
  return const_cast<Project::MaterialGraph::Graph&>(graph).serialize() != savedState;
}

bool Editor::MaterialEditor::draw(ImGuiID defDockId)
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || asset->type != Project::FileType::MATERIAL) return false;

  std::string title = std::string{ICON_MDI_PALETTE_SWATCH " "} + getName()
    + (isDirty() ? " *" : "");
  winName = title + "###MaterialEditorWin_" + std::to_string(assetUUID);

  if (firstDockTarget && !firstDockApplied) {
    ImGui::DockBuilderDockWindow(winName.c_str(), firstDockTarget);
    ImGui::SetNextWindowDockID(firstDockTarget, ImGuiCond_Always);
    firstDockApplied = true;
    firstDockFrame = false;
  } else {
    Editor::setupAssetEditorDocking(defDockId, firstDockFrame);
  }

  if (!isInit) {
    isInit = true;
    auto *mvp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
      {
        mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
        mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
      },
      ImGuiCond_FirstUseEver
    );
  }
  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen,
    ImGuiWindowFlags_NoCollapse
    | (isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0));

  // Outer split: editor body (toolbar + preview/graph splitter) on the left,
  // AssetInspector strip on the right. Replaces the global "Asset" tab the
  // scene editor used to host.
  ImVec2 outerAvail = ImGui::GetContentRegionAvail();
  float outerSplitW  = 6_px;
  float minOuterRight = 220_px;
  float minOuterLeft  = 360_px;
  float outerRightW   = std::clamp(outerAvail.x * assetSplitFrac, minOuterRight,
                                   std::max(minOuterRight, outerAvail.x - minOuterLeft - outerSplitW));
  float outerLeftW    = std::max(minOuterLeft, outerAvail.x - outerSplitW - outerRightW);

  ImGui::BeginChild("##matOuterLeft", ImVec2(outerLeftW, 0), ImGuiChildFlags_None);

  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) save();
  ImGui::SameLine();
  if (ImGui::Button(ICON_MDI_REFRESH " Recompile")) recompileCache();
  ImGui::SameLine();
  ImGui::TextDisabled("(stored in %s)", asset->path.c_str());

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
  }

  // Unreal-style layout: 3D preview on the left, graph on the right.
  // previewSplitFrac is the fraction of width allotted to the left pane.
  ImVec2 fullAvail = ImGui::GetContentRegionAvail();
  float splitterW = 6_px;
  float leftW  = std::max(180_px, (fullAvail.x - splitterW) * previewSplitFrac);
  float rightW = std::max(240_px, fullAvail.x - splitterW - leftW);
  (void)rightW;

  recompileCache();

  // Left pane: 3D preview (top) + compiled-state summary (bottom).
  ImGui::BeginChild("##matPreviewPane", ImVec2(leftW, 0), ImGuiChildFlags_Borders);

  ImGui::TextDisabled("Live Preview");

  // 3D viewport takes ~60% of the left pane vertically; the compiled-state
  // readout sits beneath it. The minimum keeps the preview from collapsing
  // when the user shrinks the editor window.
  float paneAvailY = ImGui::GetContentRegionAvail().y;
  float previewW   = ImGui::GetContentRegionAvail().x;
  float previewH   = std::max(120_px, paneAvailY * 0.60f);

  preview.setMaterial(compiledCache);
  preview.draw(ImVec2(previewW, previewH));

  ImGui::Separator();
  ImGui::TextDisabled("Compiled Values");

  // Prim/env colour swatches still help confirm "the right colour came out
  // of the graph" at a glance, even with the 3D preview above.
  ImVec4 prim{compiledCache.primColor.value.x, compiledCache.primColor.value.y,
              compiledCache.primColor.value.z, compiledCache.primColor.value.w};
  ImVec4 env{ compiledCache.envColor.value.x,  compiledCache.envColor.value.y,
              compiledCache.envColor.value.z,  compiledCache.envColor.value.w};
  ImGui::TextUnformatted("Prim Color");
  ImGui::ColorButton("##primSw", prim, ImGuiColorEditFlags_AlphaBar, ImVec2(-1, 22_px));
  ImGui::TextUnformatted("Env Color");
  ImGui::ColorButton("##envSw", env, ImGuiColorEditFlags_AlphaBar, ImVec2(-1, 22_px));

  ImGui::Separator();

  // Compact flag-state dump.
  ImGui::Text("CC:    %s",   compiledCache.ccSet.value        ? "set" : "default");
  ImGui::Text("Blend: %s",   compiledCache.blenderSet.value   ? "set" : "default");
  ImGui::Text("Z:     %s",   compiledCache.zmodeSet.value     ? "set" : "default");
  ImGui::Text("AA:    %s",   compiledCache.aaSet.value        ? "set" : "default");
  ImGui::Text("AClip: %s",   compiledCache.alphaCompSet.value ? "set" : "default");
  ImGui::Text("Tex0:  %s",   compiledCache.tex0.set.value     ? "bound" : "(none)");
  ImGui::Text("Tex1:  %s",   compiledCache.tex1.set.value     ? "bound" : "(none)");
  ImGui::Text("VFX:   %d",   compiledCache.vertexFX.value);
  ImGui::Text("Flags: 0x%X", compiledCache.drawFlags.value);

  ImGui::EndChild();

  // Vertical splitter.
  ImGui::SameLine();
  ImGui::InvisibleButton("##matSplit", ImVec2(splitterW, -1));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    splitDragging = true;
    float dx = ImGui::GetIO().MouseDelta.x;
    if (fullAvail.x > splitterW * 2) {
      previewSplitFrac += dx / (fullAvail.x - splitterW);
      previewSplitFrac = std::clamp(previewSplitFrac, 0.15f, 0.70f);
    }
  } else {
    splitDragging = false;
  }
  if (ImGui::IsItemHovered() || splitDragging) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(splitDragging ? ImGuiCol_SeparatorActive : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
      {(a.x + b.x) * 0.5f - 1.0f, a.y},
      {(a.x + b.x) * 0.5f + 1.0f, b.y},
      col
    );
  }

  // Right pane: graph canvas takes the remaining width.
  ImGui::SameLine();
  ImGui::BeginChild("##matGraphCanvas", ImVec2(0, 0), ImGuiChildFlags_None);
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  graph.graph.setSize(canvasSize);
  graph.graph.update();
  ImGui::EndChild();

  ImGui::EndChild(); // ##matOuterLeft

  ImGui::SameLine();
  ImGui::InvisibleButton("##matAssetSplit", ImVec2(outerSplitW, -1));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    assetSplitDragging = true;
    float dx = ImGui::GetIO().MouseDelta.x;
    if (outerAvail.x > outerSplitW * 2) {
      assetSplitFrac -= dx / (outerAvail.x - outerSplitW);
      assetSplitFrac = std::clamp(assetSplitFrac, 0.15f, 0.50f);
    }
  } else {
    assetSplitDragging = false;
  }
  if (ImGui::IsItemHovered() || assetSplitDragging) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(assetSplitDragging ? ImGuiCol_SeparatorActive : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
      {(a.x + b.x) * 0.5f - 1.0f, a.y},
      {(a.x + b.x) * 0.5f + 1.0f, b.y},
      col
    );
  }

  ImGui::SameLine();
  ImGui::BeginChild("##matInspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
  Editor::AssetInspector::draw(assetUUID);
  ImGui::EndChild();

  ImGui::End();
  return isOpen;
}

void Editor::MaterialEditor::save()
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset || !asset->materialAsset) return;

  recompileCache();
  asset->materialAsset->graphJSON = graph.serialize();
  asset->materialAsset->compiled  = compiledCache;
  Utils::FS::saveTextFile(asset->path, asset->materialAsset->serialize());

  savedState = asset->materialAsset->graphJSON;

  // Reload every model that pins a slot to this material. Without this,
  // edits would only propagate at next project (re)open. Same UUID-driven
  // pattern AssetManager uses for prefab variant resolution.
  uint64_t matUUID = assetUUID;
  for (auto &typed : ctx.project->getAssets().getEntries()) {
    for (auto &e : typed) {
      if (e.type != Project::FileType::MODEL_3D) continue;
      if (!e.conf.data.contains("materialAssetRefs")) continue;
      auto &refs = e.conf.data["materialAssetRefs"];
      bool referenced = false;
      for (auto it = refs.begin(); it != refs.end(); ++it) {
        if (it.value().get<uint64_t>() == matUUID) { referenced = true; break; }
      }
      if (referenced) {
        ctx.project->getAssets().reloadAssetByUUID(e.getUUID());
      }
    }
  }

  // Invalidate the asset browser's thumbnail so it re-renders with the new
  // compiled state next frame.
  if (ctx.editorScene) {
    ctx.editorScene->getMatThumbnails().invalidate(assetUUID);
  }

  Utils::Logger::log("Saved Material: " + asset->name);
}

void Editor::MaterialEditor::discardUnsavedChanges()
{
  graph.deserialize(savedState);
  recompileCache();
}

void Editor::MaterialEditor::focus() const
{
  ImGui::SetWindowFocus(("###MaterialEditorWin_" + std::to_string(assetUUID)).c_str());
}
