/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <cstdint>
#include <span>

#include "ImNodeFlow.h"
#include "../project/graph/nodeStyles.h"

// UE-Blueprint-style "Add Node" palette. Two graph kinds (script-event
// and material) feed this with their own entry tables; the UI is
// generic over both. Replaces the alpha-sorted Selectable list that
// used to live inline in each editor's right-click and dropped-link
// popup callbacks.
namespace Editor::NodePalette
{
  using TypeMask = uint32_t;
  inline TypeMask maskOf(::Project::Graph::PinDataType t) {
    return TypeMask{1u} << static_cast<uint32_t>(t);
  }

  struct Entry
  {
    uint32_t    typeIndex;  // index into the graph's NODE_TABLE
    const char* name;       // display label (icon prefix already baked in)
    const char* category;   // group header in the categorised view
    TypeMask    inTypes;    // OR of pin types this node accepts as IN
    TypeMask    outTypes;   // OR of pin types this node produces as OUT
  };

  // Render the palette body. Caller is responsible for opening / ending
  // the surrounding ImGui popup. Returns true and writes the chosen
  // index into `outTypeIndex` when the user picks an entry; the caller
  // is then expected to ImGui::CloseCurrentPopup().
  //
  // When `dragSourcePin` is non-null the popup was opened by a dropped
  // link. Entries are filtered to those whose pins can accept the drag
  // source's type at the matching direction (out-pin drag favours nodes
  // with a matching IN; in-pin drag favours nodes with a matching OUT).
  bool draw(std::span<const Entry> entries,
            ImFlow::Pin* dragSourcePin,
            uint32_t* outTypeIndex);

  // Locate the first input pin on `node` whose style matches `dragSrc`'s
  // style. Returns nullptr if no compatible slot exists. Editors use
  // this after spawning a new node to wire it to the drag source on the
  // correct slot rather than always the 0th input.
  ImFlow::Pin* firstMatchingInputPin(ImFlow::BaseNode* node, ImFlow::Pin* dragSrc);
}
