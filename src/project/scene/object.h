/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>
#include <utility>
#include <vector>

#include "json.hpp"
#include "../../utils/aabb.h"
#include "../../utils/prop.h"
#include "../component/components.h"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/matrix_decompose.hpp"

namespace Project
{
  class Scene;

  class Object
  {
    public:
      // runtime stores the component count as a u8
      static constexpr size_t MAX_COMPONENTS = 255;

      Object* parent{nullptr};

      std::string name{};
      uint32_t uuid{0};
      // Runtime-only object id. Assigned during the project build (Scene::assignRuntimeIds);
      // it is never set, generated or persisted in the editor. Do not rely on it outside of build.
      uint16_t runtimeId{};

      PROP_U64(uuidPrefab);

      PROP_VEC3(pos);
      PROP_QUAT(rot);
      PROP_VEC3(scale);

      // visibility layer mask, cameras only draw objects matching their own mask
      Property<uint32_t> visMask{"visMask", 1};

      bool proportionalScale{false};
      bool enabled{true};
      bool selectable{true};

      // Marker for the start of a screen-space 2D subtree (Godot Canvas
      // convention). When true on an Object, the build pipeline tags this
      // Object and every descendant with ObjectFlags::RENDER_LAYER_2D so the
      // runtime draws their components in DrawLayer::use2D() instead of the
      // 3D pass. pos.x/pos.y are pixel coordinates (320×240 framebuffer);
      // pos.z is draw-order depth within the 2D layer.
      bool isCanvas2D{false};

      // Anchor for 2D nodes. Maps to a 9-cell origin within the framebuffer
      // applied at runtime as an offset to obj.pos. 0 = top-left, 1 = top-
      // center, 2 = top-right, 3 = mid-left, 4 = center, 5 = mid-right,
      // 6 = bottom-left, 7 = bottom-center, 8 = bottom-right.
      uint8_t anchor2D{0};

      // Per-Object draw layer for 2D rendering. Maps to DrawLayer::use2D(idx).
      // 0 keeps the default 2D layer; >0 routes the component's RDP commands
      // into a separate queue (e.g. a pause-overlay layer above the HUD).
      // The scene config's layerCount2D bounds the maximum.
      uint8_t layerIndex2D{0};

      // True for nodes that were materialized as part of a prefab subtree on
      // instantiation (Scene::addPrefabInstance). User-added "Add Object"
      // children of a prefab instance keep this false so they remain editable
      // and survive prefab refreshes. The flag is also what distinguishes
      // refreshable nodes when re-materializing instances after a prefab edit.
      bool fromPrefab{false};

      std::unordered_map<uint64_t, GenericValue> propOverrides{};

      // Per-instance overrides for prefab class variables (Blueprint-style).
      // Keyed by PrefabVarDef::uuid (stable across variable renames). Only
      // populated on objects that are instances of a prefab with variables.
      std::unordered_map<uint64_t, GenericValue> varOverrides{};

      std::vector<std::shared_ptr<Object>> children{};
      std::vector<Component::Entry> components{};

      explicit Object(Object& parent) : parent{&parent} {}
      Object() = default;

      void addComponent(int compID);
      void removeComponent(uint64_t uuid);

      nlohmann::json serialize() const;
      void deserialize(Scene *scene, nlohmann::json &doc);

      bool isPrefabInstance() const {
        return uuidPrefab.value != 0;
      }

      // Authoring targets the outermost cascade layer, the prefab instance being edited.
      // An override on a deeply nested object is stored on that instance with a
      // path-relative key the build resolves identically. Falls back to this object's own
      // map when no instance context is active, such as direct transform edits.
      template<typename T>
      void addPropOverride(const Property<T>& prop)
      {
        GenericValue genVal{};
        genVal.set<T>(prop.value);
        if(const auto *layer = PropScope::authorLayer()) {
          // Only the scoped key. The bare key is a different slot, the instance's own
          // placement, and must not be touched.
          auto *map = const_cast<std::unordered_map<uint64_t, GenericValue>*>(layer->overrides);
          (*map)[PropScope::combine(layer->pathHash, prop.id)] = genVal;
        } else {
          propOverrides[prop.id] = genVal;
        }
      }

      template<typename T>
      void removePropOverride(const Property<T>& prop) {
        if(const auto *layer = PropScope::authorLayer()) {
          const_cast<std::unordered_map<uint64_t, GenericValue>*>(layer->overrides)
            ->erase(PropScope::combine(layer->pathHash, prop.id));
        } else {
          propOverrides.erase(prop.id);
        }
      }

      template<typename T>
      bool hasPropOverride(const Property<T>& prop) const {
        if(const auto *layer = PropScope::authorLayer()) {
          // Only the scoped key. The bare key is a different slot, the instance's own
          // transform, so checking it would falsely report nested props as overridden.
          return layer->overrides->contains(PropScope::combine(layer->pathHash, prop.id));
        }
        return propOverrides.contains(prop.id);
      }

      Utils::AABB getLocalAABB() const {
        Utils::AABB aabb{};
        bool hasVolume = false;
        for (const auto &entry : components) {
          PropScope::Dispatch enabledScope(propOverrides, entry.uuid);
          if (!entry.enabled.resolve(*this)) continue;
          const auto &info = Component::TABLE[entry.id];
          if (!info.funcGetAABB) continue;
          PropScope::Dispatch dispatchScope(propOverrides, entry.uuid);
          Utils::AABB compAABB = info.funcGetAABB(const_cast<Object&>(*this), const_cast<Component::Entry&>(entry));
          aabb.addPoint(compAABB.min);
          aabb.addPoint(compAABB.max);
          hasVolume = true;
        }

        if(!hasVolume ||
           std::isinf(aabb.min.x) || std::isinf(aabb.min.y) || std::isinf(aabb.min.z) ||
           std::isinf(aabb.max.x) || std::isinf(aabb.max.y) || std::isinf(aabb.max.z)) {
          aabb.min = {-1,-1,-1};
          aabb.max = {1,1,1};
        }
        return aabb;
      }

      Utils::AABB getWorldAABB() {
        Utils::AABB aabb = getLocalAABB();
        glm::vec3 t = pos.resolve(propOverrides);
        glm::quat r = rot.resolve(propOverrides);
        glm::vec3 s = scale.resolve(propOverrides);
        aabb.transform(t, r, s);
        return aabb;
      }
  };
}
