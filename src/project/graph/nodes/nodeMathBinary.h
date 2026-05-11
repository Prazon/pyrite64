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

  // Emit a float-typed scalar result. In pure-pass mode the expression
  // is inlined into the globalVar initializer (no exec line); in normal
  // mode the globalVar is zero-initialized and assigned inside the
  // NODE block. Centralizes the if (asPure) split so per-node
  // build/buildAsPure pairs stay one-line wrappers.
  inline void emitFloat(BuildCtx &ctx, uint64_t uuid,
                        const std::string &cExpr, bool asPure)
  {
    std::string resVar = "res_" + Utils::toHex64(uuid);
    if (asPure) {
      ctx.globalVar("float", resVar, std::string{"(float)("} + cExpr + ")");
    } else {
      ctx.globalVar("float", resVar, 0.0f);
      ctx.line(resVar + " = (float)(" + cExpr + ");");
    }
  }

  inline void emitInt(BuildCtx &ctx, uint64_t uuid,
                      const std::string &cExpr, bool asPure)
  {
    std::string resVar = "res_" + Utils::toHex64(uuid);
    if (asPure) {
      ctx.globalVar("int", resVar, std::string{"(int)("} + cExpr + ")");
    } else {
      ctx.globalVar("int", resVar, 0);
      ctx.line(resVar + " = (int)(" + cExpr + ");");
    }
  }

  // Two-input value resolver. Returns operand strings or "0" fallback.
  inline std::pair<std::string,std::string> resolveAB(BuildCtx &ctx)
  {
    std::string a = "0", b = "0";
    if (ctx.inValUUIDs && ctx.inValUUIDs->size() >= 2) {
      a = resOrZero(ctx.inValUUIDs->at(0));
      b = resOrZero(ctx.inValUUIDs->at(1));
    }
    return {a, b};
  }

  inline std::string resolveA(BuildCtx &ctx)
  {
    if (ctx.inValUUIDs && !ctx.inValUUIDs->empty()) return resOrZero(ctx.inValUUIDs->at(0));
    return "0";
  }
}
