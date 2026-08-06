/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once

#include <functional>
#include <algorithm>
#include "ImNodeFlow.h"
#include "json.hpp"
#include "IconsMaterialDesignIcons.h"
#include "../../../utils/string.h"
#include "../nodeStyles.h"
#include "../valueTypes.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace Project { class Prefab; }

namespace Project::Graph
{
  struct BuildCtx
  {
    struct VarDef
    {
      std::string type{};
      std::string name{};
      std::string value{};
    };

    std::string source{};
    std::vector<VarDef> vars{};
    // Extra #include directives a node needs (e.g. for a custom value type's C++ header).
    // Each entry is the text after "#include ", e.g. "<myType.h>".
    std::vector<std::string> includes{};
    std::vector<uint64_t> *outUUIDs{nullptr};
    std::vector<uint64_t> *inValUUIDs{nullptr};
    // Statement emitted when an execution path dead-ends (unconnected exec out).
    // Inside a loop body the emitter overrides this behaviour with continue;.
    std::string flowEnd{"return;"};
    // Value-input type ids / inline-field literals (value-pin order). Set by
    // spec-driven nodes around their own build call; fork C++ nodes leave
    // these null and use the res_ convention directly.
    std::vector<std::string> *inValTypes{nullptr};
    std::vector<std::string> *inValFallbacks{nullptr};

    // Back-propagation resolvers, set by Graph::build. valueResolver maps a
    // producer node uuid to the C++ expression of its value output (the
    // res_<uuid> globalVar in this codebase); valueTypeResolver returns the
    // producer's value-type id ("" = untyped, converts as identity).
    std::function<std::string(uint64_t)> valueResolver{};
    std::function<std::string(uint64_t)> valueTypeResolver{};
    // Graph variables (Set/Get nodes): lvalue expression and type id by name.
    std::function<std::string(const std::string&)> varLValue{};
    std::function<std::string(const std::string&)> varTypeOf{};

    bool hasValueInput(size_t i) const {
      return inValUUIDs && i < inValUUIDs->size() && (*inValUUIDs)[i] != 0;
    }

    std::string inputType(size_t i) const {
      return (inValTypes && i < inValTypes->size()) ? (*inValTypes)[i] : std::string{};
    }
    std::string inputCType(size_t i) const {
      return Node::cTypeOf(inputType(i));
    }

    // Resolves value input 'i' to a C++ expression (converted to its type). When
    // unconnected: 'fallback', else its inline-field literal, else a typed zero.
    std::string inputExpr(size_t i, const std::string &fallback = "") {
      if(hasValueInput(i) && valueResolver) {
        uint64_t producer = (*inValUUIDs)[i];
        std::string expr = valueResolver(producer);
        std::string from = valueTypeResolver ? valueTypeResolver(producer) : std::string{};
        return Node::convertExpr(from, inputType(i), expr);
      }
      if(!fallback.empty())return fallback;

      if(inValFallbacks && i < inValFallbacks->size() && !(*inValFallbacks)[i].empty()) {
        return (*inValFallbacks)[i];
      }
      std::string t = inputType(i);
      return t.empty() ? std::string{"0"} : (Node::cTypeOf(t) + "{}");
    }

    // True when emitting a body node inside a structured loop's
    // for/while block. Flips return-on-fall-off into continue so a
    // body chain that terminates naturally just iterates instead of
    // exiting the whole function. See graph.cpp's loop-inlining pass.
    bool insideLoopBody{false};

    inline std::string toStr(auto value)
    {
      std::string valStr;
      if constexpr (std::is_same_v<decltype(value), std::string>) {
        return value;
      } else {
        return std::to_string(value);
      }
    }

    BuildCtx& localConst(const std::string &type, const std::string &varName, auto value) {
      source += "    constexpr "+type+" " + varName + " = " + toStr(value) + ";\n";
      return *this;
    }

    BuildCtx& localVar(const std::string &type, const std::string &varName, auto value) {
      source += "    "+type+" " + varName + " = " + toStr(value) + ";\n";
      return *this;
    }

    BuildCtx& setVar(const std::string &varName, auto value)
    {
      source += "    " + varName + " = " + toStr(value) + ";\n";
      return *this;
    }

    BuildCtx& incrVar(const std::string &varName, auto value)
    {
      source += "    " + varName + " += " + toStr(value) + ";\n";
      return *this;
    }

    BuildCtx& globalVar(const std::string &type, const std::string &name, auto initVal)
    {
      vars.push_back(VarDef{type, name, toStr(initVal)});
      return *this;
    }

    std::string globalVar(const std::string &type, auto initVal) {
      std::string varName = "gv_" + std::to_string(vars.size());
      globalVar(type, varName, initVal);
      return varName;
    }

    BuildCtx& jump(uint32_t outIndex) {
      // An unconnected (or out-of-range) exec output ends this execution
      // path. Inside a loop body that iterates the loop instead of leaving
      // the whole function; otherwise flowEnd applies (default "return;").
      uint64_t uuidOut = (outUUIDs && outIndex < outUUIDs->size()) ? (*outUUIDs)[outIndex] : 0;
      if(uuidOut) {
        source += "    goto NODE_" + Utils::toHex64(uuidOut) + ";\n";
      } else {
        source += insideLoopBody ? "    continue;\n" : ("    " + flowEnd + "\n");
      }
      return *this;
    }

    BuildCtx& line(const std::string &str) {
      source += "    " + str + "\n";
      return *this;
    }

    // Declares a persistent variable once; duplicate names are ignored.
    BuildCtx& declareVar(const std::string &type, const std::string &name, auto initVal) {
      for(const auto &v : vars) if(v.name == name) return *this;
      vars.push_back(VarDef{type, name, toStr(initVal)});
      return *this;
    }

    BuildCtx& include(const std::string &path) {
      if(std::find(includes.begin(), includes.end(), path) == includes.end())
        includes.push_back(path);
      return *this;
    }
  };
}

namespace Project { class Prefab; }

namespace Project::Graph::Node
{
  extern std::shared_ptr<ImFlow::PinStyle> PIN_STYLE_LOGIC;
  extern std::shared_ptr<ImFlow::PinStyle> PIN_STYLE_VALUE;

  struct TypeLogic { };
  struct TypeValue { };

  // Prefab-graph context: the prefab and stem name this graph belongs to.
  // PrefabEventGraphEditor sets this before each draw frame so prefab-aware
  // nodes (PrefabEvent / PrefabFunc / PrefabVarGet) can list available
  // events / functions / variables in their dropdowns. Nullptr means "no
  // prefab context active" — e.g. the standalone NodeEditor — and prefab
  // nodes degrade to a free-text fallback.
  struct PrefabCtx
  {
    const ::Project::Prefab* prefab{nullptr};
    std::string prefabName{};
    std::string projectPath{};
  };
  PrefabCtx& activePrefabCtx();

  class Base : public ImFlow::BaseNode
  {
    public:
      uint64_t uuid{};
      uint32_t type{};
      std::vector<uint8_t> valInputTypes{};

      // Per-pin value-type ids in pin order, logic pins use Node::LOGIC_TYPE.
      // Optional: fork C++ nodes may leave these empty (untyped values,
      // conversions become identity); spec-driven nodes always fill them.
      std::vector<std::string> inTypes{};
      std::vector<std::string> outTypes{};

      // Whether the input pin at overall index 'i' is a value pin.
      bool isValueInput(size_t i) const {
        return i < inTypes.size() && inTypes[i] != LOGIC_TYPE;
      }

      // Type ids of value inputs only, in value-pin order.
      std::vector<std::string> valueInputTypes() const {
        std::vector<std::string> out{};
        for(const auto &t : inTypes) if(t != LOGIC_TYPE) out.push_back(t);
        return out;
      }

      // Type id of this node's first value output (its back-propagated value).
      std::string firstValueOutType() const {
        for(const auto &t : outTypes) if(t != LOGIC_TYPE) return t;
        return {};
      }

      // Value-type id of an output pin (by pointer); empty if not found.
      std::string outPinType(const ImFlow::Pin* p) {
        auto &outs = getOuts();
        for(size_t i = 0; i < outs.size(); ++i) {
          if(outs[i].get() == p) return i < outTypes.size() ? outTypes[i] : std::string{};
        }
        return {};
      }

      // Value-type id of an input pin (by pointer); empty if not found.
      std::string inPinType(const ImFlow::Pin* p) {
        auto &ins = getIns();
        for(size_t i = 0; i < ins.size(); ++i) {
          if(ins[i].get() == p) return i < inTypes.size() ? inTypes[i] : std::string{};
        }
        return {};
      }

      virtual void serialize(nlohmann::json &j) = 0;
      virtual void deserialize(nlohmann::json &j) = 0;
      virtual void build(BuildCtx &ctx) = 0;
      // Resolve dynamic pin types from the building graph before codegen (no-op by default).
      virtual void prepareBuild(BuildCtx &ctx) { (void)ctx; }

      // Stable type identifier for spec-driven nodes (e.g. "core.wait").
      // Empty for table-based C++ nodes, whose identity is the numeric type.
      virtual std::string typeId() const { return {}; }
      // Whether this is the graph entry node (emitted first during build).
      // Table-based nodes: the Start node is NODE_TABLE index 0.
      virtual bool isEntry() const { return type == 0; }
      // Value-pin expression (pull-based); empty for logic-only nodes.
      virtual std::string value(BuildCtx &ctx) { (void)ctx; return {}; }
      // Inline-field literal of each value input (value-pin order).
      virtual std::vector<std::string> valueInputLiterals() { return {}; }

      // Loop-shaped nodes (ForRange / While / ForEach) override these
      // so the build pass can emit a real C++ for / while wrapping
      // their body subgraph inline. Default returns false; build()
      // is the only emit hook for non-loop nodes.
      virtual bool isLoop() const { return false; }
      virtual void buildLoopHeader(BuildCtx &) {}
      virtual void buildLoopFooter(BuildCtx &ctx) { ctx.line("}"); }

      // Pure-evaluation hook: when canBePure() returns true and the
      // node has no incoming exec edge in the live graph, the build
      // pass calls buildAsPure() at function-top in topological-
      // dependency order instead of treating the node as an exec
      // step. The default implementation delegates to build() so
      // nodes that opt-in get the same emission unless they need a
      // distinct init form (most math nodes inline the expression
      // straight into the globalVar initializer to avoid the
      // separate "res = ...;" line that build() would emit).
      virtual bool canBePure() const { return false; }
      virtual void buildAsPure(BuildCtx &ctx) { build(ctx); }
  };
}