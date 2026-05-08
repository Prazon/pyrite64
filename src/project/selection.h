// added by SPBF64 fork
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Project
{
  class Scene;

  /**
   * Selection of scene objects, scoped to a single edit context.
   *
   * One instance lives in Context::mainSelection for the active scene; each
   * open PrefabEditor owns its own instance so its selection does not collide
   * with the main scene's. Widgets that need a selection take Selection& as a
   * parameter so the same widget code can drive any edit context.
   */
  class Selection
  {
    private:
      uint32_t primaryUUID{0};
      std::vector<uint32_t> uuids{};

    public:
      void clear()
      {
        primaryUUID = 0;
        uuids.clear();
      }

      void set(uint32_t uuid)
      {
        uuids.clear();
        if (uuid != 0) {
          uuids.push_back(uuid);
          primaryUUID = uuid;
          return;
        }
        primaryUUID = 0;
      }

      void setList(const std::vector<uint32_t> &list, uint32_t primary)
      {
        uuids = list;
        primaryUUID = primary;
        if (!isSelected(primaryUUID)) {
          primaryUUID = uuids.empty() ? 0 : uuids.back();
        }
      }

      void add(uint32_t uuid)
      {
        if (uuid == 0) return;
        if (!isSelected(uuid)) {
          uuids.push_back(uuid);
        }
        primaryUUID = uuid;
      }

      void remove(uint32_t uuid)
      {
        if (uuid == 0) return;
        auto it = std::remove(uuids.begin(), uuids.end(), uuid);
        if (it != uuids.end()) {
          uuids.erase(it, uuids.end());
        }
        if (primaryUUID == uuid) {
          primaryUUID = uuids.empty() ? 0 : uuids.back();
        }
      }

      void toggle(uint32_t uuid)
      {
        if (isSelected(uuid)) {
          remove(uuid);
        } else {
          add(uuid);
        }
      }

      [[nodiscard]] bool isSelected(uint32_t uuid) const
      {
        if (uuid == 0) return false;
        return std::find(uuids.begin(), uuids.end(), uuid) != uuids.end();
      }

      [[nodiscard]] uint32_t primary() const { return primaryUUID; }
      [[nodiscard]] const std::vector<uint32_t>& all() const { return uuids; }

      // Drop selection entries that no longer exist in the given scene.
      // Defined out-of-line because Scene::getObjectByUUID is not accessible
      // from this header without pulling in scene.h.
      void sanitize(Scene *scene);
  };
}
