/**
* @copyright 2026 - Prazon
* @license MIT
*
* Standalone .p64mat asset: a node-graph driven material that compiles down
* to a Project::Assets::Material (the same struct models use for inline
* per-slot overrides). Models reference these by UUID via their .conf
* materialAssetRefs map, so editing the asset propagates to every model on
* reload.
*/
#pragma once

#include <cstdint>
#include <string>

#include "json.hpp"
#include "material.h"

namespace Project::Assets
{
  // On-disk schema for .p64mat. The graphJSON blob is the ImNodeFlow graph
  // saved by MaterialGraph::serialize(); compiledMaterial is the result of
  // compiling that graph. Both are persisted so headless tooling (model
  // reload, asset browser previews) can resolve a material's runtime values
  // without having to re-walk the graph.
  struct MaterialAsset
  {
    uint64_t uuid{0};
    std::string graphJSON{};
    Material compiled{};

    [[nodiscard]] std::string serialize() const;
    void deserialize(const std::string &doc);
  };
}
