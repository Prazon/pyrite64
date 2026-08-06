/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>

#include "assets/assetManager.h"

namespace P64
{
  class Object;
}

namespace P64::NodeGraph
{
  typedef void (*GraphFunc)(void* arg);

  // Max number of object references a single graph can declare (see the "Object" node).
  // Object references are resolved to runtime object ids at build time and provided
  // per-instance by the NodeGraph component.
  constexpr int MAX_OBJ_REFS = 8;

  struct GraphDef;
  struct NodeDef;

  class Instance
  {
    private:
      GraphDef* graphDef{};
      coroutine_t *corot{};

    public:
      Object *object{};
      uint32_t args[2]{};
      // Runtime object ids bound by the NodeGraph component (Object node slots).
      uint16_t objRefs[MAX_OBJ_REFS]{};
      // Per-instance graph-variable blob, allocated and seeded by the
      // NodeGraph component; generated code addresses it at baked offsets.
      void* vars{};
      // Seconds the graph has spent running. Advances per active frame, so
      // time-driven nodes stay continuous across waits.
      float time{};
      uint16_t asset{};
      uint8_t repeatable{};
      // Per-frame deltaTime, refreshed before each coro_resume so the
      // generated run() can reference it via the DeltaTime node. Held
      // by reference inside run(), so it always reflects the latest
      // tick. v1 codegen still snapshots into a globalVar at function
      // entry, so values read after a Wait yield are stale until pure-
      // eval pipes value nodes through (see docs/graph-gaps.md).
      float lastDeltaTime{};

      Instance() = default;
      ~Instance();

      void load(uint16_t assetIdx);
      bool update(float deltaTime);
  };

  typedef int(*UserFunc)(uint32_t);

  void registerFunction(uint32_t strCRC32, UserFunc fn);
  UserFunc getFunction(uint64_t uuid);
}
