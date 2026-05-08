/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>
#include "objectFlags.h"
#include "event.h"
#include "sceneManager.h"

namespace P64
{
  /**
   * Game Object:
   * This the main struct used in scenes to represent all sorts of entities.
   * Objects can have multiple components attached to them, which provide functionality
   * for running game logic and drawing things.
   *
   * The exact makeup is set up in the editor, and loaded during a scene load.
   * Dynamic creation at runtime is only possible through prefabs.
   */
  class Object
  {
    friend class Scene;

    public:
      struct CompRef
      {
        uint8_t type{};
        uint8_t flags{};
        uint16_t offset{};
      };

      ~Object();

      uint16_t id{};
      uint16_t group{};
      uint16_t flags{};
      uint16_t compCount{0};

      // Prefab class-variable block. Lives at the very end of this object's
      // memory allocation, after compData. Zero on objects that aren't
      // prefab instances or whose prefab declares no class variables.
      uint16_t varCount{0};
      uint16_t varDataOffset{0}; // bytes from `this` to the variable buffer

      // Prefab UUID — the asset uuid of the Project::Prefab this object was
      // instantiated from, or 0 if this object isn't a prefab instance root.
      // Used by the event dispatch (P64::PrefabEvents::dispatch) to route
      // event firings to the right per-prefab generated dispatch function.
      uint32_t prefabUUID{0};

      // extra data, is overlapping with component data if unused
      fm_quat_t rot{};
      fm_vec3_t pos{};
      fm_vec3_t scale{};

      // component references, this is then also followed by a buffer for the actual data
      // the object allocation logic keeps extra space to fit everything

      //CompRef compRefs[];
      //uint8_t compData[];
      //uint8_t prefabVars[]; // present iff varCount > 0

      void setFlag(uint16_t flag, bool enabled) {
        if(enabled) {
          flags |= flag;
        } else {
          flags &= ~flag;
        }
      }

      /**
       * Returns pointer to the component reference table.
       * This is beyond the Object struct, but still in valid allocated memory.
       * @return pointer
       */
      [[nodiscard]] CompRef* getCompRefs() const {
        return (CompRef*)((uint8_t*)this + sizeof(Object));
      }

      /**
       * Returns pointer to the component data buffer.
       * This is beyond the Object struct, but still in valid allocated memory.
       * @return pointer
       */
      [[nodiscard]] char* getCompData() const {
        return (char*)getCompRefs() + sizeof(CompRef) * compCount;
      }

      /**
       * Locate a prefab class-variable record by its stable uuid (the uuid
       * the editor assigned to the PrefabVarDef). Returns a pointer to the
       * 20-byte value buffer for the variable, or nullptr if the object has
       * no variables or the uuid isn't found.
       *
       * The 20 bytes are laid out the same way as the editor wrote them
       * (sceneBuilder.cpp): primitive types occupy the leading bytes, vec3
       * is 12 contiguous floats, quat is 16 contiguous floats. Use the
       * template overload below for typed access.
       */
      [[nodiscard]] uint8_t* getPrefabVarBytes(uint64_t uuid) const {
        if(varCount == 0 || varDataOffset == 0) return nullptr;
        constexpr uint32_t VAR_RECORD_BYTES = 32;
        constexpr uint32_t VALUE_OFFSET = 12; // 8B uuid + 1B kind + 3B pad
        auto *base = (uint8_t*)this + varDataOffset;
        for(uint32_t i = 0; i < varCount; ++i) {
          auto *rec = base + i * VAR_RECORD_BYTES;
          uint64_t recUUID;
          // Use memcpy to avoid alignment surprises on MIPS.
          __builtin_memcpy(&recUUID, rec, sizeof(recUUID));
          if(recUUID == uuid) return rec + VALUE_OFFSET;
        }
        return nullptr;
      }

      /**
       * Typed read of a prefab class-variable. Returns the variable's value
       * cast to T, or a default-constructed T if the variable is missing or
       * the object has no variable block. Caller is responsible for matching
       * T to the variable's declared kind in the editor.
       */
      template<typename T>
      [[nodiscard]] T getPrefabVar(uint64_t uuid) const {
        T out{};
        auto *bytes = getPrefabVarBytes(uuid);
        if(bytes) __builtin_memcpy(&out, bytes, sizeof(T));
        return out;
      }

      /**
       * Returns the first component that matches the given type.
       * The type given must be component in the 'P64::Comp' namespace.
       * If no component of the given type is found, nullptr is returned.
       * @tparam T component type
       * @return pointer to component or nullptr
       */
      template<typename T>
      [[nodiscard]] T* getComponent() const {
        auto compRefs = getCompRefs();
        for (uint32_t i=0; i<compCount; ++i) {
          if(compRefs[i].type == T::ID) {
            return (T*)((char*)this + compRefs[i].offset);
          }
        }
        return nullptr;
      }

      template<typename T>
      [[nodiscard]] T* getComponent(uint32_t idx) const {
        auto compRefs = getCompRefs();
        for (uint32_t i=0; i<compCount; ++i) {
          if(compRefs[i].type == T::ID) {
            if (idx-- == 0) {
              return (T*)((char*)this + compRefs[i].offset);
            }
          }
        }
        return nullptr;
      }

      /**
       * Check if the object itself is enabled (not considering parent/group state).
       * @return true if enabled
       */
      [[nodiscard]] bool isSelfEnabled() const {
        return (flags & ObjectFlags::SELF_ACTIVE);
      }

      /**
       * Check if the object is enabled, considering parent/group state.
       * @return true enabled
       */
      [[nodiscard]] bool isEnabled() const {
        return (flags & ObjectFlags::ACTIVE) == ObjectFlags::ACTIVE;
      }

      /**
       * Changes the state of the object to be enabled or disabled.
       * Prefer this over changing flags directly, as components may need to be notified.
       * @param isEnabled true to enable, false to disable
       */
      void setEnabled(bool isEnabled);

      [[nodiscard]] bool hasChildren() const {
        return (flags & ObjectFlags::HAS_CHILDREN);
      }

      /**
       * Removes the given object from the scene.
       * This is a shortcut for SceneManager::getCurrent().removeObject(obj);
       * Note: deletion is deferred until the end of the frame.
       * @param keepChildren if true, children will recursivly be removed, else children will remain with undefined/invalid group
       */
      void remove(bool keepChildren = false);

      static Scene& getScene()
      {
        return SceneManager::getCurrent();
      }

      /**
       * Iterates over all direct children of the object.
       * If you need nested iteration, call this function recursively.
       *
       * Example:
       * \code{.cpp}
       * obj.iterChildren(obj.id, [](Object* child) {
       *   child->setEnabled(true);
       * });
       * \endcode
       *
       * Note: This function is intentionally a template with a callback.
       * Doing so generates the same ASM as a direct loops with an if+continue,
       * whereas iterators or std::view performs worse.
       * Template params are deduced automatically.
       *
       * @param parentId object id of the parent
       * @param f callback function, takes Object* as argument
       */
      template<typename F, typename SCENE = Scene>
      void iterChildren(F&& f) {
        const SCENE &sc = getScene();
        sc.iterObjectChildren(id, f);
      }

      /**
       * Returns the parent object of this object, or nullptr if none.
       *
       * @return pointer to parent object or nullptr
       */
      template<typename SCENE = Scene>
      Object* getParent()
      {
        const SCENE &sc = getScene();
        return sc.getObjectById(this->group);
      }

      /**
       * Takes a world space position and converts it into the local space of this object.
       *
       * Note that world-scace here assumes the object itself is sitting in it.
       * If you somehow have transforms before it, you need to apply those yourself.
       *
       * @param p point in world space
       * @return point in local space
       */
      [[nodiscard]] fm_vec3_t intoLocalSpace(const fm_vec3_t &p) const;

      /**
       * Converts a point from local space of this object into world space.
       * This will effectively apply pos/rot/scale of a point in local space.
       * @param p point in local space
       * @return point in world space
       */
      [[nodiscard]] fm_vec3_t outOfLocalSpace(const fm_vec3_t &p) const;

    private:
      inline bool performStateChange()
      {
        if((flags & P64::ObjectFlags::PENDING_ACTIVE_CHG))
        {
          flags &= ~P64::ObjectFlags::PENDING_ACTIVE_CHG;
          flags ^= P64::ObjectFlags::SELF_ACTIVE;
          return true;
        }
        return false;
      }
  };

  struct ObjectRef
  {
    uint32_t id{};

    [[nodiscard]] Object* get() const;

    [[nodiscard]] Object* operator->() const {
      return get();
    }

    [[nodiscard]] operator Object*() const {
      return get();
    }

    [[nodiscard]] explicit operator bool() const {
      return get() != nullptr;
    }
  };
}
