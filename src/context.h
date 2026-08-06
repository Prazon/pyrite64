/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <algorithm>
#include <atomic>
#include <functional>
#include <future>
#include <vector>

#include "project/project.h"
#include "project/selection.h"
#include "project/compile/compileErrors.h"
#include "utils/json.h"
#include "utils/jsonBuilder.h"
#include "utils/proc.h"
#include "utils/toolchain.h"
#include "SDL3/SDL.h"
#include "editor/keymap.h"
#include "editor/preferences.h"

namespace Editor
{
  class Scene;
  class ThumbnailCache;
}

namespace Renderer { class Scene; }

struct Context
{
  // Globals
  bool debugMode{false};
  Utils::Toolchain toolchain{};
  Project::Project *project{nullptr};
  Renderer::Scene *scene{nullptr};
  Editor::ThumbnailCache *thumbnails{nullptr};
  SDL_Window* window{nullptr};
  SDL_GPUDevice *gpu{nullptr};
  std::unique_ptr<Editor::Scene> editorScene{nullptr};
  bool forceVSync{false};
  bool experimentalFeatures{false};
  bool wantsProjectClose{false};

  std::string newerVersion{};
  std::atomic_bool hasNewerVersion{false};

  struct Clipboard
  {
    struct Entry {
      std::string data{};
      uint64_t refUUID{0};
    };

    std::vector<Entry> entries{};
  };

  Clipboard clipboard{};

  uint64_t timeCpuSelf{};
  uint64_t timeCpuTotal{};

  // Editor state
  uint64_t selAssetUUID{0};

  // Selection is a value type (Project::Selection) so that each edit context
  // (main scene, open prefab editors) can own its own. Widgets receive a
  // Selection& parameter rather than reaching into ctx; the helper methods
  // below delegate to mainSelection for upstream code paths.
  Project::Selection mainSelection{};

  // When non-empty, the selection targets a nested prefab-definition object below
  // the primary selection (a prefab instance). The path is the chain of definition-
  // node uuids from the instance's prefab root down to the nested node. Edits become
  // overrides on the instance, keyed by this path.
  std::vector<uint32_t> selSubPath{};

  // UUID of the object whose prefab is being edited in place, 0 when not editing.
  uint32_t prefabEditUUID{0};

  Editor::Preferences prefs{};

  // Structured node-graph compile errors. Cleared at the start of each build by
  // the build driver, populated during graph compilation, and rendered by the
  // Compile Errors panel. Logger remains a superset stream — this list is the
  // structured slice the panel can navigate from.
  Project::Compile::ErrorList compileErrors{};

  std::future<void> futureBuildRun{};

  // Actions deferred until after the current frame's GPU render
  std::vector<std::function<void()>> deferredActions{};
  void deferAction(std::function<void()> fn) { deferredActions.push_back(std::move(fn)); }
  void runDeferredActions()
  {
    auto actions = std::move(deferredActions);
    deferredActions.clear();
    for (auto &fn : actions) fn();
  }

  [[nodiscard]] bool isBuildOrRunning() const
  {
    if (futureBuildRun.valid()) {
      auto state = futureBuildRun.wait_for(std::chrono::seconds(0));
      return state != std::future_status::ready;
    }
    return false;
  }

  // Selection helpers for upstream code paths: delegate to mainSelection and
  // maintain the nested-prefab sub-path exactly as upstream's ctx-field
  // versions did. Fork widgets keep passing Selection& explicitly.

  void clearObjectSelection()
  {
    mainSelection.clear();
    selSubPath.clear();
  }

  void setObjectSelection(uint32_t uuid)
  {
    selSubPath.clear();
    mainSelection.set(uuid);
  }

  // Selects a nested prefab-definition object. `rootUuid` is the instance and `path` is
  // the chain of definition-node uuids down to the nested node.
  void setNestedSelection(uint32_t rootUuid, const std::vector<uint32_t> &path)
  {
    mainSelection.set(rootUuid);
    selSubPath = path;
  }

  void setObjectSelectionList(const std::vector<uint32_t> &uuids, uint32_t primaryUUID)
  {
    selSubPath.clear(); // a flat multi-selection is never a nested-prefab selection
    mainSelection.setList(uuids, primaryUUID);
  }

  void addObjectSelection(uint32_t uuid)
  {
    if (uuid == 0) return;
    selSubPath.clear();
    mainSelection.add(uuid);
  }

  void removeObjectSelection(uint32_t uuid)
  {
    mainSelection.remove(uuid);
  }

  void toggleObjectSelection(uint32_t uuid)
  {
    if (mainSelection.isSelected(uuid)) {
      removeObjectSelection(uuid);
    } else {
      addObjectSelection(uuid);
    }
  }

  [[nodiscard]] bool isObjectSelected(uint32_t uuid) const
  {
    return mainSelection.isSelected(uuid);
  }

  [[nodiscard]] bool isPrefabEditing(uint32_t uuid) const
  {
    return uuid != 0 && uuid == prefabEditUUID;
  }

  [[nodiscard]] const std::vector<uint32_t>& getSelectedObjectUUIDs() const
  {
    return mainSelection.all();
  }

  // Ensure that the selected object UUIDs are valid in the current scene, and update the primary accordingly
  void sanitizeObjectSelection(Project::Scene* scene)
  {
    if (!scene) {
      clearObjectSelection();
      return;
    }

    mainSelection.sanitize(scene);

    // Drop prefab-edit mode if its object is gone (deleted, or scene switched).
    if (prefabEditUUID && !scene->getObjectByUUID(prefabEditUUID)) prefabEditUUID = 0;
  }
};

extern Context ctx;
