/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "imgui.h"

namespace Editor
{
  // Forces the next-window dock placement for an asset editor on the first
  // frame after construction (so a fresh open lands as a sibling tab of the
  // Scene Editor even if imgui.ini has a stale floating entry for that
  // window's stable ID). Subsequent frames fall back to FirstUseEver, so
  // manual rearrangements made during the editor's lifetime stick.
  //
  // Deliberately does NOT set ViewportFlags_NoAutoMerge or clear
  // ViewportFlags_NoDecoration on the window class: an undocked editor
  // floating over the main window should merge into the main viewport
  // (chromeless, easy to re-dock by dragging onto a dock target) and only
  // promote to a real OS window when dragged outside the main window or onto
  // another monitor.
  inline void setupAssetEditorDocking(ImGuiID defDockId, bool& firstDockFrame)
  {
    if (defDockId) {
      ImGui::SetNextWindowDockID(
        defDockId,
        firstDockFrame ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    }
    firstDockFrame = false;
  }
}
