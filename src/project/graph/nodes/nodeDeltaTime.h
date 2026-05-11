/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  /**
   * Value node that exposes the current frame's deltaTime as a Float
   * pin. Resolves in two contexts:
   *   - Prefab event graphs: dispatch_X(self, eventType, deltaTime) is
   *     called with the per-frame delta from Scene::update.
   *   - Standalone NodeGraph script assets: run() binds
   *     `float& deltaTime = inst->lastDeltaTime;` at function entry,
   *     and the host refreshes lastDeltaTime before each coro_resume.
   * v1 codegen evaluates value pins once at function entry, so the
   * snapshot taken into res_<uuid> is the delta from the first
   * resume. Pure-eval (graph-gaps.md) lifts that limit.
   */
  class DeltaTime : public Base
  {
    public:
      constexpr static const char* NAME = ICON_MDI_TIMER_OUTLINE " Delta Time";

      DeltaTime()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::PureFunctionCall));
        addOUT<TypeValue>("", pinStyle(PinDataType::Float));
      }

      void draw() override {
        ImGui::TextDisabled("Tick deltaTime");
      }

      void serialize(nlohmann::json &) override {}
      void deserialize(nlohmann::json &) override {}

      void build(BuildCtx &ctx) override {
        auto resVar = "res_" + Utils::toHex64(uuid);
        ctx.globalVar("float", resVar, std::string{"deltaTime"});
      }
  };
}
