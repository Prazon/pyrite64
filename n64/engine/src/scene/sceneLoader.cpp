/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include <libdragon.h>
#include <cstdint>
#include <malloc.h>
#include <vector>
#include <new>
#include "scene/scene.h"
#include "lib/math.h"
#include "scene/componentTable.h"

namespace {
  constexpr uint32_t DATA_ALIGN = 8;

  struct ObjectEntry {
    uint16_t flags;
    uint16_t id;
    uint16_t group;
    uint8_t visMask;
    uint8_t layerIdx2D; // fork: only used when RENDER_LAYER_2D is set (was _padding)
    fm_vec3_t pos;
    fm_vec3_t scale;
    uint32_t packedRot;
    // data follows
  };

  struct __attribute__((packed)) ObjectEntryCamera : public ObjectEntry {
    uint16_t _padding;
    fm_vec3_t pos{};
    fm_quat_t rot{};
    float fov{};
    float near{};
    float far{};
    int16_t vpOffset[2]{};
    int16_t vpSize[2]{};
  };

  // to avoid any allocations for file names,
  // the path is stored here and changed by each load
  char scenePath[] = "rom:/p64/s0000_";

  inline void updateScenePath(uint16_t id)
  {
    scenePath[sizeof(scenePath)-5] = '0' + ((id/100) % 10);
    scenePath[sizeof(scenePath)-4] = '0' + ((id/10) % 10);
    scenePath[sizeof(scenePath)-3] = '0' + (id % 10);
  }

  inline void* loadSubFile(char type) {
    scenePath[sizeof(scenePath)-2] = type;
    scenePath[sizeof(scenePath)-1] = '\0';
    return asset_load(scenePath, nullptr);
  }
}

void P64::Scene::loadSceneConfig()
{
  updateScenePath(id);
  scenePath[sizeof(scenePath)-2] = '\0';

  {
    auto *tmp = (SceneConf*)asset_load(scenePath, nullptr);
    conf = *tmp;
    free(tmp);
  }
}

P64::Object* P64::Scene::loadObject(uint8_t* &objFile, std::function<void(Object&)> callback, bool deferComponentInit)
{
  ObjectEntry* objEntry = (ObjectEntry*)objFile;

  // pre-scan components to get total allocation size
  uint32_t allocSize = sizeof(Object);

  // some alignment logic below relies on an at a minimum 4-byte size
  static_assert(sizeof(Object) % 4 == 0);
  static_assert(sizeof(Object::CompRef) % 4 == 0);

  auto ptrIn = objFile + sizeof(ObjectEntry);
  uint32_t compCount = 0;
  uint32_t compDataSize = 0;
  while(ptrIn[1] != 0) {
    auto compId = ptrIn[0];
    auto argSize = ptrIn[1] * 4;

    assertf(compId < COMP_TABLE_SIZE, "Invalid component ID %d!", compId);
    const auto &compDef = COMP_TABLE[compId];
    assertf(compDef.getAllocSize != nullptr, "Component %d unknown!", compId);
    compDataSize += Math::alignUp(compDef.getAllocSize(ptrIn + 4), DATA_ALIGN);
    allocSize += sizeof(Object::CompRef);

    ptrIn += argSize;
    ++compCount;
  }

  // Prefab data block sits past the components terminator (4 zero bytes)
  // when the file-format flag HAS_PREFAB_VARS is set. The block carries
  // the prefab uuid + an optional run of class-variable records:
  //   uint32 prefabUUID, uint16 varCount, uint16 pad, then varCount * 32B
  uint16_t varCount = 0;
  uint32_t varDataSize = 0;
  uint32_t prefabUUIDFromFile = 0;
  if(objEntry->flags & ObjectFlags::HAS_PREFAB_VARS) {
    auto *blkHdr = ptrIn + 4; // skip the comp-terminator
    __builtin_memcpy(&prefabUUIDFromFile, blkHdr,     sizeof(prefabUUIDFromFile));
    __builtin_memcpy(&varCount,           blkHdr + 4, sizeof(varCount));
    constexpr uint32_t VAR_RECORD_BYTES = 32;
    varDataSize = (uint32_t)varCount * VAR_RECORD_BYTES;
  }

  // component data must be 8-byte aligned, GCC tries to be smart
  // and some structs cuse 64-bit writes to members.
  // if it is misaligned, add spacing after the comp table
  uint32_t offsetData = (sizeof(Object::CompRef) * compCount);
  if(allocSize % 8 != 0) {
    compDataSize += 4;
    offsetData += 4;
  }

  allocSize += compDataSize + varDataSize;

  void* objMem = memalign(DATA_ALIGN, allocSize); // @TODO: custom allocator
  memObjects += malloc_usable_size(objMem);

  if(allocSize < 16) {
    memset(objMem, 0, allocSize);
  } else {
    sys_hw_memset(objMem, 0, allocSize);
  }

  auto objCompTablePtr = (Object::CompRef*)((char*)objMem + sizeof(Object));
  auto objCompDataPtr = (char*)(objCompTablePtr) + offsetData;

  Object* obj = new(objMem) Object();
  obj->id = objEntry->id;
  obj->group = objEntry->group;
  obj->flags = objEntry->flags;
  obj->layerIdx2D = objEntry->layerIdx2D;
  obj->visMask = objEntry->visMask;
  assertf(compCount <= 0xFF, "Object %d has too many components (%d)", objEntry->id, (int)compCount);
  obj->compCount = compCount;
  obj->varCount = varCount;
  obj->prefabUUID = prefabUUIDFromFile;
  // Variable buffer offset gets filled in below once the comp-data extent
  // is known. Leave at 0 here so getPrefabVarBytes returns null if anything
  // bails out before the copy completes.
  obj->varDataOffset = 0;
  obj->pos = objEntry->pos;
  obj->scale = objEntry->scale;
  obj->rot = Math::unpackQuat(objEntry->packedRot);

  if(callback)callback(*obj);

  ptrIn = objFile + sizeof(ObjectEntry);
  while(ptrIn[1] != 0)
  {
    uint8_t compId = ptrIn[0];
    // sizeDw can exceed 63 for large components (e.g. Path with 163 dwords
    // = 652 bytes), so a uint8_t holding sizeDw*4 truncates and `ptrIn +=
    // argSize` lands mid-component, reading garbage as the next compId.
    // The pre-scan loop uses `auto` here which promotes to int — keep
    // these in sync.
    uint32_t argSize = (uint32_t)ptrIn[1] * 4u;

    const auto &compDef = COMP_TABLE[compId];
    // debugf("Alloc: comp %d (arg: %d)\n", compId, argSize);

    objCompTablePtr->type = compId;
    objCompTablePtr->flags = 0;
    objCompTablePtr->offset = objCompDataPtr - (char*)obj;
    ++objCompTablePtr;

    if(deferComponentInit)
    {
      auto &pending = pendingCompInit.emplace_back();
      pending.obj = obj;
      pending.dataPtr = objCompDataPtr;
      pending.compId = compId;
      pending.initData = ptrIn + 4;
    }
    else
    {
      compDef.initDel(*obj, objCompDataPtr, ptrIn + 4);
    }

    objCompDataPtr += Math::alignUp(compDef.getAllocSize(ptrIn + 4), 8);
    ptrIn += argSize;

    // send ready event. this is deferred, so it will always happen after 'initDel'
  }
  sendEvent(obj->id, 0, EVENT_TYPE_READY, 0);

  /*debugf("Object: id=%d | group=%d | flags=0x%04X | pos=(%f,%f,%f) | comp: %d\n",
    obj->id, obj->group, obj->flags,
    (double)obj->pos.x, (double)obj->pos.y, (double)obj->pos.z,
    compCount
  );*/

  objFile = ptrIn + 4;

  // Copy the prefab-variable records into the tail of this object's alloc
  // (right after compData). The pre-scan above reserved exactly
  // varCount*32 bytes there. Skip the 8-byte block header (uuid + count +
  // pad — already consumed in the pre-scan) and copy the fixed-size
  // records straight in. We clear the file-format flag bit so runtime
  // code never sees a marker that only matters at load time.
  if(obj->flags & ObjectFlags::HAS_PREFAB_VARS) {
    constexpr uint32_t VAR_RECORD_BYTES = 32;
    constexpr uint32_t VAR_VALUE_OFFSET = 12;
    auto *varDest = (uint8_t*)objCompDataPtr;
    obj->varDataOffset = (uint16_t)(varDest - (uint8_t*)obj);
    objFile += 8; // skip prefabUUID(4) + varCount(2) + pad(2)
    uint32_t varBytes = (uint32_t)obj->varCount * VAR_RECORD_BYTES;
    if(varBytes > 0) __builtin_memcpy(varDest, objFile, varBytes);
    objFile += varBytes;

    // Placement-new ARRAY vars so std::vector<E>'s pointers are well-
    // formed. The 20-byte value slot fits a libstdc++ vector (3 ptrs =
    // 12 B on 32-bit MIPS) with room to spare. Object::~Object pairs
    // these with ~vector calls so heap allocs unwind on scene unload.
    static_assert(sizeof(std::vector<float>) <= 20,
      "std::vector<E> exceeds 20-byte prefab-var value slot");
    for(uint32_t i = 0; i < obj->varCount; ++i) {
      auto *rec = varDest + i * VAR_RECORD_BYTES;
      uint8_t kind = rec[8];
      uint8_t elemKind = rec[9];
      if(kind == 8) {
        void *valPtr = rec + VAR_VALUE_OFFSET;
        switch(elemKind) {
          case 0: new (valPtr) std::vector<int32_t>(); break;
          case 1: new (valPtr) std::vector<float>(); break;
          case 2: new (valPtr) std::vector<bool>(); break;
        }
      }
    }
    obj->flags &= ~ObjectFlags::HAS_PREFAB_VARS;
  }

  objects.push_back(obj);
  if(obj->id < idLookup.size()) {
    idLookup[obj->id] = obj;
  }

  return obj;
}

void P64::Scene::runPendingComponentInit()
{
  for(auto &pending : pendingCompInit)
  {
    const auto &compDef = COMP_TABLE[pending.compId];
    compDef.initDel(*pending.obj, pending.dataPtr, pending.initData);
  }
  pendingCompInit.clear();
}

void P64::Scene::loadScene() {
  updateScenePath(id);
  scenePath[sizeof(scenePath)-2] = '\0';

  cameras.clear();

  //debugf("Objects: %lu\n", conf.objectCount);
  if(conf.objectCount)
  {
    auto *objFileStart = (uint8_t*)(loadSubFile('o'));

    // now process all other objects
    auto objFile = objFileStart;
    for(uint32_t i=0; i<conf.objectCount; ++i) {
      loadObject(objFile, {}, true);
    }

    std::function<void(const Object* parent, Object& obj)> updateStates = [&](const Object* parent, Object& obj)
    {
      obj.setFlag(ObjectFlags::PARENTS_ACTIVE, parent ? parent->isEnabled() : true);
      iterObjectChildren(obj.id, [&](Object* child) {
        updateStates(&obj, *child);
      });
    };

    // Resolve effective active state for the full hierarchy before deferred
    // component init so disabled parents/groups do not register physics data.
    for(auto obj : objects)
    {
      if(obj->group != 0)continue;
      updateStates(nullptr, *obj);
    }

    // run component init only after all objects are registered in the scene
    runPendingComponentInit();

    free(objFileStart);
  }
}
