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
   * Pure-function value node that exposes the current frame's deltaTime
   * as a Float pin. Only meaningful inside a prefab event graph reached
   * via the OnTick dispatch path; the per-frame Scene::update loop
   * forwards deltaTime through the generated dispatch function so the
   * identifier is in scope. Standalone NodeGraph script assets do not
   * yet thread deltaTime to their generated run() function; using this
   * node there will produce an undeclared-identifier compile error
   * which is the intentional v1 fail mode.
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
