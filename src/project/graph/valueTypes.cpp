/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "valueTypes.h"
#include "nodeStyles.h"

#include "ImNodeFlow.h"

#include <unordered_map>
#include <map>

namespace Project::Graph::Node
{
  namespace
  {
    std::unordered_map<std::string, ValueType> g_types{};
    // (from, to) -> conversion template, "{}" marks the source expression.
    std::map<std::pair<std::string, std::string>, std::string> g_conversions{};
    std::unordered_map<std::string, std::shared_ptr<ImFlow::PinStyle>> g_pinStyles{};

    std::string applyTemplate(const std::string &tmpl, const std::string &expr)
    {
      std::string out{};
      for(size_t i = 0; i < tmpl.size(); ) {
        if(tmpl[i] == '{' && i + 1 < tmpl.size() && tmpl[i + 1] == '}') {
          out += expr;
          i += 2;
        } else {
          out.push_back(tmpl[i++]);
        }
      }
      return out;
    }
  }

  void clearValueTypes()
  {
    g_types.clear();
    g_conversions.clear();
    g_pinStyles.clear();
  }

  void addValueType(const ValueType &t)
  {
    g_types[t.id] = t;
    g_pinStyles.erase(t.id); // re-derive the style from the (possibly new) color
  }

  void addConversion(const std::string &from, const std::string &to, const std::string &tmpl)
  {
    g_conversions[{from, to}] = tmpl;
  }

  void registerBuiltinValueTypes()
  {
    initNodeStyles();
    auto col = [](PinDataType t) { return pinColor(t); };
    addValueType({"i32",  "Int",    "int32_t",   "0",    col(PinDataType::Int),      4});
    addValueType({"u32",  "UInt",   "uint32_t",  "0",    col(PinDataType::Byte),     4});
    addValueType({"f32",  "Float",  "float",     "0.0f", col(PinDataType::Float),    4});
    addValueType({"vec3", "Vec3",   "fm_vec3_t", "{}",   col(PinDataType::Struct),  12});
    addValueType({"quat", "Quat",   "fm_quat_t", "{}",   col(PinDataType::Rotator), 16});
    // Own type so it can't be wired into arithmetic, despite being an integer id.
    addValueType({"objref", "Object", "uint16_t", "0",   col(PinDataType::Object),   4});
    // Fork extras used by the string / logic node groups.
    addValueType({"bool", "Bool",   "bool",      "false", col(PinDataType::Bool),    1});
    addValueType({"str",  "String", "const char*", "\"\"", col(PinDataType::String), 4});

    // Implicit numeric conversions (mirrors data/nodes/_types.js).
    addConversion("u32", "i32", "(int32_t)({})");
    addConversion("u32", "f32", "(float)({})");
    addConversion("i32", "u32", "(uint32_t)({})");
    addConversion("i32", "f32", "(float)({})");
    addConversion("f32", "u32", "(uint32_t)({})");
    addConversion("f32", "i32", "(int32_t)({})");
    addConversion("bool", "i32", "(int32_t)({})");
    addConversion("i32", "bool", "(({}) != 0)");
    addConversion("f32", "bool", "(({}) != 0.0f)");

    // Scalar broadcast to all vec3 components.
    addConversion("f32", "vec3", "fm_vec3_t{{}, {}, {}}");
    addConversion("i32", "vec3", "fm_vec3_t{(float)({}), (float)({}), (float)({})}");
    addConversion("u32", "vec3", "fm_vec3_t{(float)({}), (float)({}), (float)({})}");
  }

  const ValueType* findValueType(const std::string &id)
  {
    auto it = g_types.find(id);
    return it != g_types.end() ? &it->second : nullptr;
  }

  bool isLogicType(const std::string &id)
  {
    return id == LOGIC_TYPE || id.empty();
  }

  bool canConnect(const std::string &from, const std::string &to)
  {
    if(isLogicType(from) || isLogicType(to)) {
      return isLogicType(from) && isLogicType(to);
    }
    if(from == to)return true;
    return g_conversions.find({from, to}) != g_conversions.end();
  }

  std::string convertExpr(const std::string &from, const std::string &to, const std::string &expr)
  {
    if(from == to || isLogicType(from) || isLogicType(to))return expr;
    auto it = g_conversions.find({from, to});
    if(it == g_conversions.end())return expr;
    return applyTemplate(it->second, expr);
  }

  std::string cTypeOf(const std::string &id)
  {
    const auto *t = findValueType(id);
    return t ? t->cType : std::string{"int"};
  }

  int byteSizeOf(const std::string &id)
  {
    const auto *t = findValueType(id);
    return t ? t->size : 4;
  }

  ImU32 colorOf(const std::string &id)
  {
    const auto *t = findValueType(id);
    return t ? t->color : IM_COL32(0xFF, 0x99, 0x55, 0xFF);
  }

  std::shared_ptr<ImFlow::PinStyle> pinStyleForValueType(const std::string &id)
  {
    auto it = g_pinStyles.find(id);
    if(it != g_pinStyles.end()) return it->second;

    if(isLogicType(id)) {
      auto ps = pinStyle(PinDataType::Exec);
      g_pinStyles[id] = ps;
      return ps;
    }

    // Same geometry as the palette's value pins (nodeStyles.cpp), coloured by
    // the registered type. Custom JS types therefore get first-class pins.
    auto ps = std::make_shared<ImFlow::PinStyle>(colorOf(id), 0, 5.5f, 6.5f, 6.0f, 1.3f);
    ps->extra.padding.y = 16;
    g_pinStyles[id] = ps;
    return ps;
  }
}
