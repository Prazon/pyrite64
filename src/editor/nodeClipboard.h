/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <string>

// Per-graph-kind clipboards for cut/copy/paste of node selections.
// Two separate stores so a script-graph blob can never deserialize
// against the material-graph node table by accident; the editors
// route their hotkey calls into the matching one.
namespace Editor::NodeClipboard
{
  // In-process JSON blobs holding the most recent selection cut/copied
  // in each graph kind. Empty means nothing to paste.
  std::string& scriptGraph();
  std::string& materialGraph();
}
