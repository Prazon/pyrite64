/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
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
}

namespace Renderer { class Scene; }

struct Context
{
  // Globals
  bool debugMode{false};
  Utils::Toolchain toolchain{};
  Project::Project *project{nullptr};
  Renderer::Scene *scene{nullptr};
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

  // SPBF64 fork: selection is now a value type (Project::Selection) so that
  // each edit context (main scene, open prefab editors) can own its own.
  // Widgets receive a Selection& parameter rather than reaching into ctx.
  Project::Selection mainSelection{};

  Editor::Preferences prefs{};

  // Structured node-graph compile errors. Cleared at the start of each build by
  // the build driver, populated during graph compilation, and rendered by the
  // Compile Errors panel. Logger remains a superset stream — this list is the
  // structured slice the panel can navigate from.
  Project::Compile::ErrorList compileErrors{};

  std::future<void> futureBuildRun{};

  [[nodiscard]] bool isBuildOrRunning() const
  {
    if (futureBuildRun.valid()) {
      auto state = futureBuildRun.wait_for(std::chrono::seconds(0));
      return state != std::future_status::ready;
    }
    return false;
  }
};

extern Context ctx;
