/**
* Editor 2D viewport — Godot-style screen-space canvas preview.
*
* Sibling tab to the existing 3D-Viewport. Walks the active scene tree,
* and for every Object whose `isCanvas2D` ancestry chain qualifies it as a
* 2D node, paints a stand-in for each of its 2D components (Sprite2D /
* Label2D / ProgressBar2D) using ImGui's draw list. No GPU pass — the
* preview is intentionally a 1:1 representation of what `rdpq_*` would
* paint at runtime, drawn into the editor's existing ImGui surface.
*
* Picking + drag-to-move route through the host's Selection / UndoRedo so
* the panel composes with the inspector and undo stack the same way the
* 3D viewport does.
*/
#pragma once
#include <cstdint>
#include "imgui.h"

namespace Project { class Scene; class Object; class Selection; }

namespace Editor
{
  class Viewport2D
  {
    private:
      // View transform — origin in window coords + zoom multiplier. Pan via
      // middle-drag, zoom via Ctrl+wheel. Defaults centered the canvas in
      // the panel on first frame (recomputed when the panel is bigger or
      // smaller than the canvas).
      float zoom{2.0f};
      ImVec2 origin{0, 0};
      bool   originInit{false};

      // Drag state for moving a selected node by its transform.
      bool   dragging{false};
      uint32_t dragObjectUUID{0};
      ImVec2 dragMouseStart{};
      ImVec2 dragObjStartPos{};

      // Pan state.
      bool   panActive{false};

      // Hover info — the topmost 2D object under the cursor this frame.
      uint32_t hoveredUUID{0};

      // Cached framebuffer dims of the active scene; refreshed each draw.
      int fbW{320};
      int fbH{240};

      // Convert canvas pixel coords -> ImGui window coords.
      ImVec2 canvasToScreen(ImVec2 c) const;
      ImVec2 screenToCanvas(ImVec2 s) const;

      // Walk and paint. `paintObject` recurses; tracks the hovered candidate
      // so the last-painted (i.e. topmost in draw order) object wins.
      void paintObject(Project::Scene &scene, Project::Selection &sel,
                       Project::Object &obj, ImDrawList *dl,
                       bool inCanvas, ImVec2 anchorOffset);

      // Single-component paint — dispatched by component id. Updates an
      // optional bounding box used for hover hit-testing and the selection
      // overlay rectangle.
      void paintComponent(Project::Object &obj, int compId, void *data,
                          ImDrawList *dl, ImVec2 originScreen,
                          ImVec2 *outMin, ImVec2 *outMax);

    public:
      Viewport2D();

      void draw();
  };
}
