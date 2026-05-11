/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "nodeStyles.h"

#include <array>

namespace Project::Graph
{
  namespace
  {
    // Canonical UE5 pin colors. Values lifted from blueprintue.com's
    // /bue-render/render.css (.exec / .bool / .int / etc rules) which
    // mirrors the editor's K2 schema palette.
    constexpr ImU32 col(uint8_t r, uint8_t g, uint8_t b) {
      return IM_COL32(r, g, b, 0xFF);
    }

    ImU32 pinColorRaw(PinDataType t)
    {
      switch (t) {
        case PinDataType::Exec:      return col(0xFF, 0xFF, 0xFF);
        case PinDataType::Bool:      return col(0x95, 0x00, 0x00);
        case PinDataType::Byte:      return col(0x00, 0x6F, 0x65);
        case PinDataType::Int:       return col(0x1F, 0xE3, 0xAF);
        case PinDataType::Int64:     return col(0xAC, 0xE3, 0xAF);
        case PinDataType::Float:     return col(0xA1, 0xFF, 0x45);
        case PinDataType::Double:    return col(0x38, 0xD5, 0x00);
        case PinDataType::String:    return col(0xFF, 0x00, 0xD4);
        case PinDataType::Name:      return col(0xCD, 0x82, 0xFF);
        case PinDataType::Text:      return col(0xE7, 0x7C, 0xAA);
        case PinDataType::Object:    return col(0x00, 0xAA, 0xF5);
        case PinDataType::Class:     return col(0x59, 0x00, 0xBC);
        case PinDataType::Interface: return col(0xF1, 0xFF, 0xAA);
        case PinDataType::Struct:    return col(0x00, 0x59, 0xCC);
        case PinDataType::Delegate:  return col(0xFF, 0x38, 0x38);
        case PinDataType::Rotator:   return col(0xA0, 0xB4, 0xFF);
        case PinDataType::Wildcard:  return col(0x7F, 0x78, 0x78);
        case PinDataType::MatProp:   return col(0x00, 0x59, 0xCC); // = Struct
      }
      return col(0xFF, 0xFF, 0xFF);
    }

    constexpr size_t PIN_COUNT = static_cast<size_t>(PinDataType::MatProp) + 1;
    std::array<std::shared_ptr<ImFlow::PinStyle>, PIN_COUNT> g_pinStyles{};
    bool g_initDone = false;

    // Exec uses a triangle (3-segment circle in ImNodeFlow's renderer,
    // see ImNodeFlow.inl ::AddCircleFilled call). Data pins use the
    // smooth-circle path (segment count = 0, ImGui auto-tessellated).
    std::shared_ptr<ImFlow::PinStyle> makePinStyle(PinDataType t)
    {
      const bool isExec = (t == PinDataType::Exec);
      const int   shape = isExec ? 3 : 0;
      const float baseR = isExec ? 6.0f : 5.5f;
      const float hoverR = isExec ? 7.0f : 6.5f;
      const float connR = isExec ? 6.5f : 6.0f;
      const float thick = isExec ? 1.6f : 1.3f;
      auto ps = std::make_shared<ImFlow::PinStyle>(
        pinColorRaw(t), shape, baseR, hoverR, connR, thick);
      // Vertical pin padding (matches the legacy value the editor
      // windows used) so node row heights stay consistent.
      ps->extra.padding.y = 16;
      // Slightly thicker exec wires than data wires, like UE.
      if (isExec) {
        ps->extra.link_thickness = 3.0f;
        ps->extra.link_hovered_thickness = 4.0f;
      }
      return ps;
    }
  }

  void initNodeStyles()
  {
    if (g_initDone) return;
    for (size_t i = 0; i < PIN_COUNT; ++i) {
      g_pinStyles[i] = makePinStyle(static_cast<PinDataType>(i));
    }
    g_initDone = true;
  }

  std::shared_ptr<ImFlow::PinStyle> pinStyle(PinDataType t)
  {
    initNodeStyles();
    return g_pinStyles[static_cast<size_t>(t)];
  }

  ImU32 pinColor(PinDataType t) { return pinColorRaw(t); }

  ImU32 categoryColor(NodeCategory cat)
  {
    switch (cat) {
      case NodeCategory::Event:               return col(0xFF, 0x00, 0x00);
      case NodeCategory::FunctionCall:        return col(0x79, 0xC9, 0xFF);
      case NodeCategory::PureFunctionCall:    return col(0xAA, 0xEE, 0xA0);
      case NodeCategory::ParentFunctionCall:  return col(0xFF, 0x72, 0x00);
      case NodeCategory::FunctionTerminator:  return col(0xCC, 0x00, 0xFF);
      case NodeCategory::ExecBranch:          return col(0xFF, 0xFF, 0xFF);
      case NodeCategory::ExecSequence:        return col(0xE8, 0xAA, 0xAA);
      case NodeCategory::Macro:               return col(0xFF, 0xFF, 0xFF);
      case NodeCategory::Cast:                return col(0x13, 0x74, 0x79);
      case NodeCategory::Switch:              return col(0xFF, 0xFF, 0x00);
      case NodeCategory::Timeline:            return col(0xFF, 0xB1, 0x00);
      case NodeCategory::BreakStruct:         return col(0x00, 0x59, 0xCC);
      case NodeCategory::Result:              return col(0xFF, 0xD3, 0xAA);
      case NodeCategory::MaterialGraphRoot:   return col(0xFF, 0xDA, 0xB4);
      case NodeCategory::MaterialConstant:    return col(0x90, 0x76, 0x23);
      case NodeCategory::Comment:             return IM_COL32(0xFF, 0xFF, 0xFF, 0x33);
      case NodeCategory::Variable:            return col(0x1F, 0xE3, 0xAF);
    }
    return col(0xFF, 0xFF, 0xFF);
  }

  std::shared_ptr<ImFlow::NodeStyle> makeNodeStyle(NodeCategory cat)
  {
    const ImU32 header = categoryColor(cat);

    // Pick legible title text per header luminance. UE uses dark text on
    // pale headers (white/yellow/pure-func-green) and white text on the
    // saturated dark headers (red/blue/cast-teal/etc).
    const float r = ((header >>  0) & 0xFF) / 255.0f;
    const float g = ((header >>  8) & 0xFF) / 255.0f;
    const float b = ((header >> 16) & 0xFF) / 255.0f;
    const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
    const ImColor titleCol = lum > 0.6f
      ? ImColor(20, 20, 20, 255)
      : ImColor(245, 245, 245, 255);

    // UE event / pure-function shape is a strong pill. Everything else
    // gets the standard rounded-rect look.
    const bool pill = (cat == NodeCategory::Event
                    || cat == NodeCategory::PureFunctionCall);
    const float radius = pill ? 12.0f : 5.0f;

    auto ns = std::make_shared<ImFlow::NodeStyle>(header, titleCol, radius);
    // Dark grey body to match blueprintue's dark canvas look. The
    // ImNodeFlow default is a bluish slate (55,64,75); we want the
    // header colour to dominate.
    ns->bg = IM_COL32(0x20, 0x22, 0x1F, 0xFF);
    ns->border_color = IM_COL32(0, 0, 0, 180);
    if (cat == NodeCategory::Comment) {
      // Comment nodes: translucent body, no strong header. Keeps the
      // contained-area aesthetic without yet implementing real
      // resize/contain logic (that's a later phase).
      ns->bg = IM_COL32(0xFF, 0xFF, 0xFF, 0x14);
      ns->border_color = IM_COL32(0xFF, 0xFF, 0xFF, 0x66);
    }
    return ns;
  }
}
