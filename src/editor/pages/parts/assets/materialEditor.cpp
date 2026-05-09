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

#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"
#include "../../../../project/materialGraph/nodes/baseNode.h"
#include "../../../imgui/helper.h"

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
  // Bronze pin colour for material props — distinct from the green
  // (logic) and brown (value) pin styles used by the script/event graph.
  // Same shape so users get muscle memory between graph kinds.
  auto &style = *Project::MaterialGraph::Node::PIN_STYLE_MATPROP;
  style = ImFlow::PinStyle{
    IM_COL32(0xFF, 0xC8, 0x66, 0xFF),
    0, 6.0f, 7.0f, 6.5f, 1.3f
  };
  style.extra.padding.y = 16;

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

  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  if (firstDockTarget && !firstDockApplied) {
    ImGui::DockBuilderDockWindow(winName.c_str(), firstDockTarget);
    ImGui::SetNextWindowDockID(firstDockTarget, ImGuiCond_Always);
    firstDockApplied = true;
  } else if (defDockId) {
    ImGui::SetNextWindowDockID(defDockId, ImGuiCond_FirstUseEver);
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

  if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) save();
  ImGui::SameLine();
  if (ImGui::Button(ICON_MDI_REFRESH " Recompile")) recompileCache();
  ImGui::SameLine();
  ImGui::TextDisabled("(stored in %s)", asset->path.c_str());

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
  }

  // Bottom-pinned preview pane + top graph canvas. Mirrors ModelEditor's
  // splitter layout so the ergonomics carry over.
  ImVec2 fullAvail = ImGui::GetContentRegionAvail();
  float splitterH = 6_px;
  float bottomH = std::max(80_px, (fullAvail.y - splitterH) * previewSplitFrac);
  float topH = std::max(120_px, fullAvail.y - splitterH - bottomH);

  // Top: graph canvas.
  ImGui::BeginChild("##matGraphCanvas", ImVec2(0, topH), ImGuiChildFlags_None);
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  graph.graph.setSize(canvasSize);
  graph.graph.update();
  ImGui::EndChild();

  // Splitter.
  ImGui::InvisibleButton("##matSplit", ImVec2(-1, splitterH));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    splitDragging = true;
    float dy = ImGui::GetIO().MouseDelta.y;
    if (fullAvail.y > splitterH * 2) {
      previewSplitFrac -= dy / (fullAvail.y - splitterH);
      previewSplitFrac = std::clamp(previewSplitFrac, 0.15f, 0.85f);
    }
  } else {
    splitDragging = false;
  }
  if (ImGui::IsItemHovered() || splitDragging) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }
  {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(splitDragging ? ImGuiCol_SeparatorActive : ImGuiCol_Separator);
    ImGui::GetWindowDrawList()->AddRectFilled(
      {a.x, (a.y + b.y) * 0.5f - 1.0f},
      {b.x, (a.y + b.y) * 0.5f + 1.0f},
      col
    );
  }

  // Bottom: live preview + compiled summary. Recompile on every frame
  // is cheap (graph is small) and means edits show up immediately.
  ImGui::BeginChild("##matPreviewPane", ImVec2(0, bottomH), ImGuiChildFlags_Borders);
  recompileCache();

  ImGui::TextDisabled("Live Preview");
  ImGui::SameLine();
  ImGui::TextDisabled("(compiled values shown below)");

  ImGui::Columns(2, "##matPreviewCols", true);
  ImGui::SetColumnWidth(0, 220_px);

  // Left column: a colour swatch sampling primColor/envColor — useful when
  // those drive the CC. The on-device renderer can do far more than this
  // can show, but a swatch is enough to confirm "yes, picking the right
  // green made the green show up".
  ImVec4 prim{compiledCache.primColor.value.x, compiledCache.primColor.value.y,
              compiledCache.primColor.value.z, compiledCache.primColor.value.w};
  ImVec4 env{ compiledCache.envColor.value.x,  compiledCache.envColor.value.y,
              compiledCache.envColor.value.z,  compiledCache.envColor.value.w};
  ImGui::TextUnformatted("Prim Color");
  ImGui::ColorButton("##primSw", prim, ImGuiColorEditFlags_AlphaBar, ImVec2(180_px, 28_px));
  ImGui::TextUnformatted("Env Color");
  ImGui::ColorButton("##envSw", env, ImGuiColorEditFlags_AlphaBar, ImVec2(180_px, 28_px));

  ImGui::NextColumn();

  // Right column: dump of the compiled Material's flag-state — easiest
  // way to see what the graph actually produced before there's a real
  // 3D preview wired in.
  ImGui::Text("Color Combiner: %s",  compiledCache.ccSet.value      ? "set" : "default");
  ImGui::Text("Blender:        %s",  compiledCache.blenderSet.value ? "set" : "default");
  ImGui::Text("Z-Mode:         %s",  compiledCache.zmodeSet.value   ? "set" : "default");
  ImGui::Text("AA:             %s",  compiledCache.aaSet.value      ? "set" : "default");
  ImGui::Text("Alpha-Clip:     %s",  compiledCache.alphaCompSet.value ? "set" : "default");
  ImGui::Text("Tex0:           %s",  compiledCache.tex0.set.value   ? "bound" : "(none)");
  ImGui::Text("Tex1:           %s",  compiledCache.tex1.set.value   ? "bound" : "(none)");
  ImGui::Text("Vertex FX:      %d",  compiledCache.vertexFX.value);
  ImGui::Text("Draw Flags:     0x%X", compiledCache.drawFlags.value);

  ImGui::Columns(1);
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
