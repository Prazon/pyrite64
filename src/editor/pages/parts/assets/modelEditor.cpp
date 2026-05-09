/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "modelEditor.h"

#include <filesystem>

#include "libdragon.h"
#include "ccMapping.h"
#include "textureEditor.h"
#include "assetEditorDocking.h"
#include "../../../../context.h"
#include "../../../imgui/helper.h"
#include "../../editorScene.h"
#include "imgui_internal.h"

namespace
{
  ImVec2 DEF_WIN_SIZE{520, 620};

  constexpr auto Z_MODES = "None\0Read\0Write\0Read+Write\0";
  constexpr auto AA_MODES = "None\0Standard\0Reduced\0";

  constexpr auto DITHER_MODES = "Square / Square\0"
    "Square / Inv. Square\0"
    "Square / Noise\0"
    "Square / None\0"
    "Bayer / Bayer\0"
    "Bayer / Inv. Bayer\0"
    "Bayer / Noise\0"
    "Bayer / None\0"
    "Noise / Square\0"
    "Noise / Inv. Square\0"
    "Noise / Noise\0"
    "Noise / None\0"
    "None / Bayer\0"
    "None / Inv. Bayer\0"
    "None / Noise\0"
    "None / None\0";

  constexpr auto VERTEX_EFFECTS =
    "None\0"
    "Spherical UV\0"
    "Cel-shade Color\0"
    "Cel-shade Alpha\0"
    "Outline\0"
    "UV Offset\0";

  void toggleProp(const char* name, bool &propState, auto cb)
  {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();

    ImGui::PushFont(nullptr, 18.0_px);

    if(ImGui::IconButton(
      propState
      ? ICON_MDI_CHECKBOX_MARKED_CIRCLE
      : ICON_MDI_CHECKBOX_BLANK_CIRCLE_OUTLINE,
      {24_px,24_px},
      ImVec4{1,1,1,1}
    )) {
      propState = !propState;
      //Editor::UndoRedo::getHistory().markChanged("Edit " + name);
    }
    ImGui::PopFont();
    ImGui::SameLine();

    ImGui::SameLine();
    ImGui::Text("%s", name);
    ImGui::TableSetColumnIndex(1);

    if(!propState)ImGui::BeginDisabled();
    ImGui::PushID(name);
    cb();
    ImGui::PopID();
    if(!propState)ImGui::EndDisabled();
  }

  template<typename T>
  void toggleProp(const char* name, bool &propState, Property<T> &prop)
  {
    toggleProp(name, propState, [&prop](){
      ImTable::typedInput(&prop.value);
    });
  }

  void printCC(const char* a, const char* b, const char* c, const char* d)
  {
    auto nonZero = [](const char* s){ return s[0] != '0'; };

    std::string s{};
    // check if mul does something
    if(nonZero(c) && (nonZero(a) || nonZero(b)))
    {
      if(nonZero(a) && nonZero(b)) {
        s += std::string{"("} + a + " - " + b + ")";
      } else {
        s += nonZero(a) ? a : b;
      }
      s += std::string{" * "} + c;
    }

    if(nonZero(d)) {
      if(!s.empty())s += " + ";
      s += d;
    }
    if(s.empty())s = "0";

    ImGui::Text("%s", s.c_str());
  }
}

bool Editor::ModelEditor::draw(ImGuiID defDockId)
{
  auto &assetManager = ctx.project->getAssets();
  auto model = assetManager.getEntryByUUID(assetUUID);
  if(!model)return false;

  // Stable ImGui ID via ###suffix so renaming the asset (display title) doesn't
  // throw away the window's saved position/dock state. The Win suffix also
  // invalidates pre-multi-viewport imgui.ini entries that had no ### at all.
  // Plain asset filename (no "Model:" prefix, no icon glyphs): the title
  // string is also what the SDL backend pushes to the OS title bar when the
  // window is undocked, and unrenderable icon glyphs surface there as
  // square placeholder boxes.
  winName = model->name
    + "###ModelEditorWin_" + std::to_string(assetUUID);

  // Dock as a sibling tab of Scene Editor on first open; let undocked
  // floating instances merge into the main viewport when over it (see
  // assetEditorDocking.h).
  Editor::setupAssetEditorDocking(defDockId, firstDockFrame);

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
  ImGui::Begin(winName.c_str(), &isOpen);

  // SPBF64 fork: keep the preview viewport's mesh in sync with the asset.
  // Rebind if the UUID changed OR if mesh3D was null at first bind and is
  // now present (assets can finish loading after the window opens).
  void* meshRaw = model->mesh3D.get();
  if (previewBoundUUID != model->getUUID() || previewBoundMesh != meshRaw) {
    preview.setMesh(model->getUUID(), model->mesh3D, &model->model);
    previewBoundUUID = model->getUUID();
    previewBoundMesh = meshRaw;
  }

  // Top half: 3D preview, bottom half: material/property UI.
  ImVec2 fullAvail = ImGui::GetContentRegionAvail();
  float splitterH = 6_px;
  float topH = std::max(80_px, (fullAvail.y - splitterH) * previewSplitFrac);
  float bottomH = std::max(80_px, fullAvail.y - splitterH - topH);

  ImGui::BeginChild("##preview", ImVec2(0, topH), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  preview.draw(ImGui::GetContentRegionAvail());
  ImGui::EndChild();

  // Draggable splitter
  ImGui::InvisibleButton("##split", ImVec2(-1, splitterH));
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    splitDragging = true;
    float dy = ImGui::GetIO().MouseDelta.y;
    if (fullAvail.y > splitterH * 2) {
      previewSplitFrac += dy / (fullAvail.y - splitterH);
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

  ImGui::BeginChild("##matUI", ImVec2(0, bottomH), ImGuiChildFlags_None);

  ImVec2 labelWidth = {89_px, -1.0f};
  bool needsReload = false;

  auto subSection = [&labelWidth](const char* name, auto cb)
  {
    if(ImGui::CollapsingSubHeader(name, ImGuiTreeNodeFlags_DefaultOpen) && ImTable::start(name, nullptr, labelWidth))
    {
      cb();
      ImTable::end();
    }
  };

  std::string matToRemove{};
  for(auto &entry : model->model.materials)
  {
    auto &mat = entry.second;

    // Lookup the bound material asset (if any) up front so the collapsing
    // header can show what's wired and the thumbnail row has it in scope.
    uint64_t curAssetRef = 0;
    if (model->conf.data.contains("materialAssetRefs")
      && model->conf.data["materialAssetRefs"].contains(entry.first))
    {
      curAssetRef = model->conf.data["materialAssetRefs"][entry.first].get<uint64_t>();
    }
    Project::AssetManagerEntry* boundAssetEntry =
      curAssetRef != 0 ? assetManager.getEntryByUUID(curAssetRef) : nullptr;

    // Header summarises the current binding so collapsed slots are still
    // informative (Unreal does the same in the Details panel). The ###id
    // suffix keeps the open/close state stable as the visible binding
    // text changes.
    std::string headerLabel = "Material: " + entry.first;
    if (boundAssetEntry) {
      headerLabel += "  >  " + boundAssetEntry->name;
    } else if (mat.isCustom.value) {
      headerLabel += "  >  (inline override)";
    } else {
      headerLabel += "  >  (default)";
    }
    headerLabel += "###Slot_" + entry.first;

    ImGui::PushID(("MaterialSlot_" + entry.first).c_str());
    if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
      // Per-slot material-asset binding. When set, the asset's compiled
      // Material wins over the inline override at model-load time (see
      // AssetManager::reloadEntry FileType::MATERIAL handling). Mutually
      // exclusive with the inline Override toggle: enabling one disables
      // the other so the runtime path is unambiguous.
      auto &matAssets = assetManager.getTypeEntries(Project::FileType::MATERIAL);
      uint64_t beforeRef = curAssetRef;

      // Thumbnail tile (left). Falls back to a palette-icon button when
      // nothing is bound, and accepts material-asset drops in either state.
      ImVec2 tileSize{56_px, 56_px};
      ImGui::PushID("##binding");
      SDL_GPUTexture* tileTex = nullptr;
      if (boundAssetEntry && boundAssetEntry->materialAsset && ctx.editorScene) {
        tileTex = ctx.editorScene->getMatThumbnails().fetch(
          curAssetRef, tileSize, boundAssetEntry->materialAsset->compiled);
      }
      bool tileClicked = false;
      if (tileTex) {
        tileClicked = ImGui::ImageButton("##tile", ImTextureRef(tileTex), tileSize);
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button,
          ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        tileClicked = ImGui::Button(ICON_MDI_PALETTE_SWATCH "##tile_empty", tileSize);
        ImGui::PopStyleColor();
      }
      if (ImGui::BeginDragDropTarget()) {
        if (auto* p = ImGui::AcceptDragDropPayload("ASSET")) {
          uint64_t dropUUID = *(uint64_t*)p->Data;
          auto* dropEntry = assetManager.getEntryByUUID(dropUUID);
          if (dropEntry && dropEntry->type == Project::FileType::MATERIAL) {
            curAssetRef = dropUUID;
          }
        }
        ImGui::EndDragDropTarget();
      }
      if (tileClicked && curAssetRef != 0 && ctx.editorScene) {
        ctx.editorScene->openMaterialEditor(curAssetRef);
      }

      // Right of tile: stacked combo + action buttons.
      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::TextDisabled("Material Asset");
      ImGui::SetNextItemWidth(-1.0f);
      ImTable::addAssetVecComboBox<Project::AssetManagerEntry>(
        "", matAssets, curAssetRef
      );
      if (ImGui::Button(ICON_MDI_PLUS " New")) {
        namespace fs = std::filesystem;
        fs::path matRoot = fs::path(ctx.project->getPath()) / "assets";
        std::string base = "Material";
        std::string chosen = base + "_X";
        for (int i = 1; i < 1000; ++i) {
          std::string n = (i == 1) ? base : (base + "_" + std::to_string(i));
          if (!fs::exists(matRoot / (n + ".p64mat"))) { chosen = n; break; }
        }
        uint64_t newUUID = assetManager.createMaterial(chosen);
        if (newUUID) curAssetRef = newUUID;
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(curAssetRef == 0);
      if (ImGui::Button(ICON_MDI_CLOSE " Clear")) {
        curAssetRef = 0;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(curAssetRef == 0);
      if (ImGui::Button(ICON_MDI_PENCIL " Edit") && ctx.editorScene && curAssetRef != 0) {
        ctx.editorScene->openMaterialEditor(curAssetRef);
      }
      ImGui::EndDisabled();
      ImGui::EndGroup();
      ImGui::PopID();

      if (curAssetRef != beforeRef) {
        if (curAssetRef == 0) {
          if (model->conf.data.contains("materialAssetRefs")) {
            model->conf.data["materialAssetRefs"].erase(entry.first);
          }
        } else {
          if (!model->conf.data.contains("materialAssetRefs")) {
            model->conf.data["materialAssetRefs"] = nlohmann::json::object();
          }
          model->conf.data["materialAssetRefs"][entry.first] = curAssetRef;
          // Asset wins over inline. Drop any previous override panel state
          // so the user doesn't see a stale inline section below.
          mat.isCustom.value = false;
          model->conf.data["materials"].erase(entry.first);
        }
        assetManager.markAssetMetaDirty(model->getUUID());
        needsReload = true;
      }

      ImGui::Spacing();
      ImTable::start("Override", nullptr, labelWidth);
      if (ImTable::addProp("Override", mat.isCustom)) {
        if (mat.isCustom.value) {
          // Enabling inline override while a material asset is set would
          // make the runtime path ambiguous; clear the asset ref first.
          if (curAssetRef != 0 && model->conf.data.contains("materialAssetRefs")) {
            model->conf.data["materialAssetRefs"].erase(entry.first);
          }
          model->conf.data["materials"][entry.first] = mat.serialize();
        } else {
          matToRemove = entry.first; // defer to not break loop
        }
        assetManager.markAssetMetaDirty(model->getUUID());
      }
      ImTable::end();
      if (mat.isCustom.value) {
        ImGui::TextDisabled(ICON_MDI_PENCIL " Editing inline override (saved on this model)");
      }

      if(!mat.isCustom.value)
      {
        ImGui::PopID();
        continue;
      }

      auto oldMat = mat;

      auto usage = N64::CC::getUsage(mat.cc.value);
      // enforce disabling unused values
      if(!usage.prim)mat.primColorSet.value = false;
      if(!usage.env)mat.envColorSet.value = false;
      if(!usage.k4k5)mat.k4k5Set.value = false;
      if(!usage.lod)mat.primLodSet.value = false;

      subSection("Color-Combiner", [&]
      {
        ImTable::add("2-Cycle");

        glm::ivec4 cc[2], cca[2];
        N64::CC::unpackCC(mat.cc.value, cc[0], cca[0], cc[1], cca[1]);

        if(ImGui::Checkbox("##2C", &usage.twoCycle) && usage.twoCycle)
        {
          // if we enable 2-cycle mode, force a pass-through by default
          cc[1][0] = N64::CC::NAMES_COL_A.size() - 1;
          cc[1][1] = N64::CC::NAMES_COL_B.size() - 1;
          cc[1][2] = N64::CC::NAMES_COL_C.size() - 1;
          cc[1][3] = 0;

          cca[1][0] = N64::CC::NAMES_ALPHA_A.size() - 1;
          cca[1][1] = N64::CC::NAMES_ALPHA_B.size() - 1;
          cca[1][2] = N64::CC::NAMES_ALPHA_C.size() - 1;
          cca[1][3] = 0;
        }

        for(int c = 0; c < (usage.twoCycle ? 2 : 1); ++c)
        {
          ImGui::PushID(c);
          ImTable::add("A");
          ImGui::SideBySide(
            [&]{ ImGui::Combo("##C0C_A",  &cc[c][0], N64::CC::NAMES_COL_A.data(), N64::CC::NAMES_COL_A.size()); },
            [&]{ ImGui::Combo("##C0A_A", &cca[c][0], N64::CC::NAMES_ALPHA_A.data(), N64::CC::NAMES_ALPHA_A.size()); }
          );
          ImTable::add("B");
          ImGui::SideBySide(
            [&]{ ImGui::Combo("##C0C_B",  &cc[c][1], N64::CC::NAMES_COL_B.data(), N64::CC::NAMES_COL_B.size()); },
            [&]{ ImGui::Combo("##C0A_B", &cca[c][1], N64::CC::NAMES_ALPHA_B.data(), N64::CC::NAMES_ALPHA_B.size()); }
          );
          ImTable::add("C");
          ImGui::SideBySide(
            [&]{ ImGui::Combo("##C0C_C",  &cc[c][2], N64::CC::NAMES_COL_C.data(), N64::CC::NAMES_COL_C.size()); },
            [&]{ ImGui::Combo("##C0A_C", &cca[c][2], N64::CC::NAMES_ALPHA_C.data(), N64::CC::NAMES_ALPHA_C.size()); }
          );
          ImTable::add("D");
          ImGui::SideBySide(
            [&]{ ImGui::Combo("##C0C_D",  &cc[c][3], N64::CC::NAMES_COL_D.data(), N64::CC::NAMES_COL_D.size()); },
            [&]{ ImGui::Combo("##C0A_D", &cca[c][3], N64::CC::NAMES_ALPHA_D.data(), N64::CC::NAMES_ALPHA_D.size()); }
          );
          ImGui::PopID();

          ImTable::add("Color");
          printCC(
            N64::CC::NAMES_COL_A[cc[c][0]], N64::CC::NAMES_COL_B[cc[c][1]],
            N64::CC::NAMES_COL_C[cc[c][2]], N64::CC::NAMES_COL_D[cc[c][3]]
          );
          ImTable::add("Alpha");
          printCC(
            N64::CC::NAMES_ALPHA_A[cca[c][0]], N64::CC::NAMES_ALPHA_B[cca[c][1]],
            N64::CC::NAMES_ALPHA_C[cca[c][2]], N64::CC::NAMES_ALPHA_D[cca[c][3]]
         );

          if(usage.twoCycle && c == 0) {
            ImGui::Dummy({0, 4_px});
          }
        }

        if(!usage.twoCycle) {
          cc[1] = cc[0];
          cca[1] = cca[0];
        }

        mat.cc.value = N64::CC::packCC(cc[0], cca[0], cc[1], cca[1]);
        if(usage.twoCycle) {
          mat.cc.value |= RDPQ_COMBINER_2PASS;
        }
      });

      auto drawMatTex = [&](Project::Assets::MaterialTex &tex, uint32_t id) {
        ImGui::PushID(id + 0xFF);

        ImTable::add("Placeholder");
        ImGui::Combo("##PH", &tex.dynType.value, "None\0" "Tile\0" "Texture + Tile\0");

        if(tex.dynType.value == tex.DYN_TYPE_FULL) {
          ImTable::addProp("Size", tex.texSize);
        } else {
          TextureEditor::draw(tex);
        }

        ImGui::PopID();
      };

      mat.tex0.set.value = usage.tex0;
      mat.tex1.set.value = usage.tex1;
      if(usage.tex0)subSection("Texture 0", [&]{ drawMatTex(mat.tex0, 0); });
      if(usage.tex1)subSection("Texture 1", [&]{ drawMatTex(mat.tex1, 1); });

      subSection("Sampling", [&]
      {
        toggleProp("Perspect.", mat.perspSet.value, mat.persp);

        toggleProp("Dither", mat.ditherSet.value, [&] {
          ImGui::Combo("##Dither", &mat.dither.value, DITHER_MODES);
        });

        toggleProp("Filtering", mat.filterSet.value, [&] {
          int val = mat.filter.value == 0 ? 0 : 1; // map 2->1
          ImGui::Combo("##", &val, "Nearest\0Bilinear\0");
          mat.filter.value = val == 0 ? 0 : 2;
        });
      });

      if(usage.prim || usage.env || usage.lod || usage.k4k5)
      {
        subSection("Values", [&]
        {
          if(usage.prim)toggleProp("Prim", mat.primColorSet.value, mat.primColor);
          if(usage.env)toggleProp("Env", mat.envColorSet.value, mat.envColor);
          if(usage.lod)toggleProp("LOD", mat.primLodSet.value, mat.primLod);
          if(usage.k4k5)toggleProp("K4/K5", mat.k4k5Set.value, mat.k4k5);

          mat.primLod.value = glm::clamp(mat.primLod.value, 0u, 255u);
          mat.k4k5.value = glm::clamp(mat.k4k5.value, 0, 255);
        });
      }

      subSection("Geometry Modes", [&]
      {
        ImTable::add("Vertex FX");
        ImGui::Combo("##Vert", &mat.vertexFX.value, VERTEX_EFFECTS);

        ImTable::add("Unlit");
        ImGui::CheckboxFlags("##Unlit", &mat.drawFlags.value, T3D::FLAG_NO_LIGHT);

        ImTable::addProp("Fog to Alpha", mat.fogToAlpha);

        ImTable::add("Cull-Front");
        ImGui::CheckboxFlags("##CF", &mat.drawFlags.value, T3D::FLAG_CULL_FRONT);
        ImTable::add("Cull-Back");
        ImGui::CheckboxFlags("##CB", &mat.drawFlags.value, T3D::FLAG_CULL_BACK);
      });

      subSection("Render Modes", [&]
      {
        toggleProp("Alpha-Clip", mat.alphaCompSet.value, [&] {
          ImGui::SliderInt("##AC", &mat.alphaComp.value, 0, 255,
            mat.alphaComp.value == 0 ? "<Off>" : "%d"
          );
        });

        toggleProp("Depth", mat.zmodeSet.value, [&] {
          ImGui::Combo("##", &mat.zmode.value, Z_MODES);
        });

        toggleProp("Anti-Alias", mat.aaSet.value, [&] {
          ImGui::Combo("##AA", &mat.aa.value, AA_MODES);
        });

        toggleProp("Blending", mat.blenderSet.value, [&]
        {
          std::vector<ImTable::ComboEntry> blenders{
            {0, "None (Opaque)"},
            {RDPQ_BLENDER_MULTIPLY, "Multiply (Alpha)"},
            {RDPQ_BLENDER_ADDITIVE, "Additive"},
          };
          ImTable::addVecComboBox("", blenders, mat.blender.value);
        });

        toggleProp("Fog", mat.fogSet.value, [&]
        {
          std::vector<ImTable::ComboEntry> fogs{
            {0, "None"},
            {RDPQ_FOG_STANDARD, "Fog (Standard)"},
          };
          ImTable::addVecComboBox("", fogs, mat.fog.value);
        });

        toggleProp("Fixed-Z", mat.zprimSet.value, [&] {
          ImGui::SideBySide(
            [&]{ ImGui::InputInt("##0", &mat.zprim.value); },
            [&]{ ImGui::InputInt("##1", &mat.zdelta.value); }
          );
        });
      });

      ImGui::Dummy({0, 2_px});

      if(mat.isCustom.value && oldMat != mat) {
        model->conf.data["materials"][entry.first] = mat.serialize();
        assetManager.markAssetMetaDirty(model->getUUID());

        if(oldMat.tex0.texSize != mat.tex0.texSize) {
          needsReload = true;
        }
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();  // ##matUI
  ImGui::End();

  // update placeholder indices
  uint32_t slot = 0;
  for(auto &entry : model->model.materials)
  {
    auto &mat = entry.second;
    if(mat.isCustom.value)
    {
      if(mat.tex0.dynType.value) {
        mat.tex0.dynPlaceholder.value = slot++;
        if(slot >= 8)break;
      }
      if(mat.tex1.dynType.value) {
        mat.tex1.dynPlaceholder.value = slot++;
        if(slot >= 8)break;
      }
    }
  }

  if(!matToRemove.empty())
  {
    model->conf.data["materials"].erase(matToRemove);
    assetManager.markAssetMetaDirty(model->getUUID());
    assetManager.save();
    assetManager.reloadAssetByUUID(model->getUUID());
  }

  if(needsReload)
  {
    assetManager.save();
    assetManager.reloadAssetByUUID(model->getUUID());
  }

  return isOpen;
}

void Editor::ModelEditor::focus() const
{
  ImGui::SetWindowFocus(winName.c_str());
}
