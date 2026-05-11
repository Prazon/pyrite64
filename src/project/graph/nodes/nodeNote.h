/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  // Comment frame: a resizable, coloured rectangle that visually groups
  // the nodes inside it. The colored fill is painted by the editor on
  // the background draw list (so it sits behind regular nodes); this
  // class owns the geometry, title, and colour. Dragging the frame in
  // the editor moves any non-selected child nodes whose centre lay
  // inside the frame's previous-frame rect (Editor::CommentFrames).
  class Note : public Base
  {
    public:
      // Public so Editor::CommentFrames can read them while painting
      // and propagating drag deltas. Defaults match what UE Blueprint
      // ships: translucent grey at ~20% alpha.
      std::string text{};
      ImVec2      size{260, 160};
      uint32_t    color{IM_COL32(0x88, 0x88, 0x88, 0x40)};

      constexpr static const char* NAME = ICON_MDI_CLIPBOARD_OUTLINE " Comment";

      Note()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        // Body needs to be fully transparent so only the externally-
        // painted frame rect is visible. Comment category from Phase 4
        // already gave us a faint grey fill; override with full-alpha
        // zero here so nothing shows behind the title input.
        auto ns = makeNodeStyle(NodeCategory::Comment);
        ns->bg = IM_COL32(0, 0, 0, 0);
        ns->border_color = IM_COL32(0, 0, 0, 0);
        setStyle(std::move(ns));
      }

      void draw() override {
        auto editor = getHandler();
        if (!editor) return;
        float scale = editor->getGrid().scale();

        // Title input. Width tracks the user-set frame width so it
        // stays inside the painted rect even after a resize.
        ImGui::SetNextItemWidth(std::max(60.0f, size.x - 56.0f));
        ImGui::InputTextWithHint("##note_text", "Comment...", &text);
        ImGui::SameLine();
        // Compact colour picker. Using a button + popup keeps the
        // frame title bar tight; ColorEdit4 inside the popup is the
        // standard ImGui pattern for small colour swatches.
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(color);
        if (ImGui::ColorButton("##c", col,
              ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreview,
              ImVec2(20, 20))) {
          ImGui::OpenPopup("##noteColor");
        }
        if (ImGui::BeginPopup("##noteColor")) {
          if (ImGui::ColorEdit4("##picker", &col.x,
                ImGuiColorEditFlags_AlphaBar
                | ImGuiColorEditFlags_NoSidePreview
                | ImGuiColorEditFlags_NoSmallPreview)) {
            color = ImGui::ColorConvertFloat4ToU32(col);
          }
          ImGui::EndPopup();
        }

        // Spacer block claims the lower portion of the user-set frame
        // size so the ImNodeFlow node bounding box matches the painted
        // rect. The actual visible rect is painted externally by the
        // editor on the background draw list.
        const float titleH = ImGui::GetItemRectSize().y;
        const float remH = std::max(20.0f, size.y - titleH - 10.0f);

        ImGui::InvisibleButton("##fillBlock",
          ImVec2(std::max(40.0f, size.x - 32.0f), remH - 14.0f));

        // Resize handle in the bottom-right corner. Drag updates the
        // grid-space size; divide the screen-space delta by zoom so a
        // zoomed-out canvas doesn't over-scale the resize.
        ImGui::InvisibleButton("##resize", ImVec2(14, 14));
        if (ImGui::IsItemActive()
            && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          ImVec2 d = ImGui::GetIO().MouseDelta;
          size.x = std::max(80.0f, size.x + d.x / scale);
          size.y = std::max(60.0f, size.y + d.y / scale);
        }
      }

      void serialize(nlohmann::json &j) override {
        j["text"] = text;
        j["size"] = {size.x, size.y};
        j["color"] = color;
      }

      void deserialize(nlohmann::json &j) override {
        text = j.value("text", "");
        if (j.contains("size") && j["size"].is_array() && j["size"].size() == 2) {
          size.x = j["size"][0].get<float>();
          size.y = j["size"][1].get<float>();
        }
        color = j.value<uint32_t>("color", IM_COL32(0x88, 0x88, 0x88, 0x40));
      }

      void build(BuildCtx &ctx) override {
        (void)ctx;
      }
  };
}
