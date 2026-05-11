/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "nodeClipboard.h"

namespace Editor::NodeClipboard
{
  std::string& scriptGraph()   { static std::string s; return s; }
  std::string& materialGraph() { static std::string s; return s; }
}
