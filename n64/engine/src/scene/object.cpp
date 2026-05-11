/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "scene/object.h"

#include "scene/componentTable.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include <vector>

P64::Object::~Object()
{
  auto compRefs = getCompRefs();
  for (uint32_t i=0; i<compCount; ++i) {
    const auto &compDef = COMP_TABLE[compRefs[i].type];
    char* dataPtr = (char*)this + compRefs[i].offset;
    compDef.initDel(*this, dataPtr, nullptr);
  }

  // Destruct non-trivial prefab vars (ARRAY = std::vector<E>) so any
  // heap allocations they made get released with the object. Mirrors
  // the placement-new pass in sceneLoader.cpp; the kind/elemKind bytes
  // are encoded by sceneBuilder.cpp at scene-write time.
  if(varCount > 0 && varDataOffset != 0) {
    constexpr uint32_t VAR_RECORD_BYTES = 32;
    constexpr uint32_t VALUE_OFFSET = 12;
    auto *base = (uint8_t*)this + varDataOffset;
    for(uint32_t i = 0; i < varCount; ++i) {
      auto *rec = base + i * VAR_RECORD_BYTES;
      uint8_t kind = rec[8];
      uint8_t elemKind = rec[9];
      if(kind == 8) {
        void *valPtr = rec + VALUE_OFFSET;
        switch(elemKind) {
          case 0: ((std::vector<int32_t>*)valPtr)->~vector(); break;
          case 1: ((std::vector<float>*)valPtr)->~vector(); break;
          case 2: ((std::vector<bool>*)valPtr)->~vector(); break;
        }
      }
    }
  }
}

void P64::Object::setEnabled(bool isEnabled)
{
  if(isEnabled != this->isSelfEnabled()) {
    flags |= ObjectFlags::PENDING_ACTIVE_CHG;
    SceneManager::getCurrent().needsObjStateUpdate = true;
  } else {
    flags &= ~ObjectFlags::PENDING_ACTIVE_CHG;
  }
}

void P64::Object::remove(bool keepChildren)
{
  if(flags & ObjectFlags::PENDING_REMOVE)return;
  flags |= ObjectFlags::PENDING_REMOVE;
  flags &= ~(ObjectFlags::ACTIVE | ObjectFlags::PENDING_ACTIVE_CHG);
  SceneManager::getCurrent().removeObject(*this);

  if(!keepChildren)
  {
    iterChildren([keepChildren](Object* child)
    {
        if(child) child->remove(keepChildren);
    });
  }
}

fm_vec3_t P64::Object::intoLocalSpace(const fm_vec3_t &p) const
{
  fm_quat_t invRot;
  fm_quat_inverse(&invRot, &rot);

  auto res = (p  - pos);
  return invRot * res / scale;
}

fm_vec3_t P64::Object::outOfLocalSpace(const fm_vec3_t &p) const
{
  return rot * (p * scale) + pos;
}

P64::Object* P64::ObjectRef::get() const
{
  return SceneManager::getCurrent().getObjectById((uint16_t)id);
}
