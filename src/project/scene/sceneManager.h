/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <string>
#include <vector>

#include "scene.h"

namespace Project
{
  class Project;

  struct SceneEntry
  {
    int id{};
    std::string name;
    // Cached so the content browser can place scenes at their virtual
    // folder without loading every scene.json on each draw.
    std::string relPath{};

    // for combobox:
    int getId() const { return id; }
    const std::string& getName() const { return name; }
  };

  class SceneManager
  {
    private:
      std::vector<SceneEntry> entries{};
      Project *project;
      Scene *loadedScene{nullptr};
      // -1 = no pending swap. Set by requestLoad(); consumed at end of frame
      // by processPendingLoad() so we never free the live Scene mid-draw.
      int pendingLoadId{-1};

      void loadSceneInternal(int id, bool saveCurrent);

    public:
      SceneManager(Project *pr) : project{pr} {
      }

      ~SceneManager();

      void reload();
      void save();

      [[nodiscard]] const std::vector<SceneEntry> &getEntries() const { return entries; }

      void add();
      void remove(int id);
      void duplicate(int id);

      void loadScene(int id);

      // Defer a scene swap until the current draw frame finishes. Safe to call
      // from inside ImGui draw code (e.g. asset browser double-click).
      void requestLoad(int id);

      // Run any pending swap requested via requestLoad(). Prompts to save when
      // the active scene's undo history is dirty. Call once per frame, after
      // the editor's draw() has returned and any EditScope has ended.
      void processPendingLoad();

      [[nodiscard]] Scene* getLoadedScene() const { return loadedScene; }

      // Re-read a scene from disk, replacing the in-memory copy when it is
      // currently loaded. Clears the undo history and any stale selection
      // since object pointers are about to be reseated. Refreshes the
      // entry name cache as a side effect.
      void reloadFromDisk(int id);

      // Update a scene's content-browser folder. Patches in-memory if the
      // scene is currently loaded; otherwise edits scene.json directly.
      // Refreshes the entry cache so the browser sees the move immediately.
      void setSceneRelPath(int id, const std::string &newRelPath);

      // Update a scene's display name. Patches in-memory if the scene is
      // currently loaded; otherwise edits scene.json directly. Refreshes
      // the entry cache so the content browser shows the new name without
      // requiring a reload.
      void setSceneName(int id, const std::string &newName);

      // Rewrite relPath on every scene whose path equals or is nested under
      // `oldPrefix`, replacing that prefix with `newPrefix`. Used by folder
      // rename. Empty `oldPrefix` is treated as no-op.
      void renameSceneFolder(const std::string &oldPrefix, const std::string &newPrefix);

      // Return the IDs of every scene whose relPath equals or is nested
      // under `prefix`. Used by folder-delete to warn the user and clean up.
      [[nodiscard]] std::vector<int> findScenesUnder(const std::string &prefix) const;
  };
}
