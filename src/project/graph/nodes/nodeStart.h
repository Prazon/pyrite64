/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once

#include "baseNode.h"
#include "../../../utils/hash.h"

namespace Project::Graph::Node
{
  class Start : public Base
  {
    private:


    public:
      constexpr static const char* NAME = ICON_MDI_PLAY " Start";

      Start()
      {
        uuid = Utils::Hash::randomU64();
        setTitle(NAME);
        setStyle(makeNodeStyle(NodeCategory::Event));

        addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        //addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
        //addOUT<TypeLogic>("", PIN_STYLE_LOGIC);
      }

      void draw() override {
        ImGui::Text("On Start");
        //ImGui::Text("On Event");
        //ImGui::Text("On Collision");
      }

      void serialize(nlohmann::json &j) override {
      }

      void deserialize(nlohmann::json &j) override {
      }

      void build(BuildCtx &ctx) override {
      }
  };
}