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
      [[nodiscard]] Scene* getLoadedScene() const { return loadedScene; }

      // Update a scene's content-browser folder. Patches in-memory if the
      // scene is currently loaded; otherwise edits scene.json directly.
      // Refreshes the entry cache so the browser sees the move immediately.
      void setSceneRelPath(int id, const std::string &newRelPath);

      // Rewrite relPath on every scene whose path equals or is nested under
      // `oldPrefix`, replacing that prefix with `newPrefix`. Used by folder
      // rename. Empty `oldPrefix` is treated as no-op.
      void renameSceneFolder(const std::string &oldPrefix, const std::string &newPrefix);

      // Return the IDs of every scene whose relPath equals or is nested
      // under `prefix`. Used by folder-delete to warn the user and clean up.
      [[nodiscard]] std::vector<int> findScenesUnder(const std::string &prefix) const;
  };
}
