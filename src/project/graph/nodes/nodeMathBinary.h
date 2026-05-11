/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

// Shared helpers for the binary / unary / ternary math + logic nodes
// in Group A of the Pixic graph palette. Every node in this group
// follows the existing Compare / CompBool exec-required pattern: an
// exec input/output for control flow, value inputs for operands, and
// a value output via a function-top globalVar that gets assigned
// inside the node's exec block. This avoids the topo-sort that pure-
// evaluation nodes would need; downstream consumers read the latest
// res_<uuid> at exec time.
namespace Project::Graph::Node::MathHelpers
{
  // Resolve an input value uuid to the producer's res_ identifier or
  // a literal 0 fallback when the slot is unwired. The fallback keeps
  // generated code valid even when the user hasn't connected an input.
  inline std::string resOrZero(uint64_t inputUUID)
  {
    if (inputUUID == 0) return "0";
    return "res_" + Utils::toHex64(inputUUID);
  }
}
