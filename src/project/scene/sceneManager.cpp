/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "./sceneManager.h"
#include "../project.h"
#include <filesystem>

#include "../../context.h"
#include "../../utils/fs.h"
#include "../../utils/json.h"
#include "../../editor/undoRedo.h"

namespace
{
  std::string getScenePath(Project::Project *project) {
    auto scenesPath = project->getPath() + "/data/scenes";
    if (!fs::exists(scenesPath)) {
      fs::create_directory(scenesPath);
    }
    return scenesPath;
  }
}

void Project::SceneManager::reload()
{
  entries.clear();

  auto scenesPath = getScenePath(project);

  // list directories
  for (const auto &entry : fs::directory_iterator{scenesPath}) {
    if (entry.is_directory()) {
      auto path = entry.path();
      auto name = path.filename().string();

      std::string relPath{};
      try {
        auto sceneJsonPath = path / "scene.json";

        auto doc = Utils::JSON::loadFile(sceneJsonPath);
        if (doc.is_object())
        {
          auto docConf = doc["conf"];

          auto scName = docConf.value("name", "");
          if(!scName.empty()) {
            name = scName;
          }
          relPath = doc.value("relPath", std::string{});
        }
      } catch(std::exception &e) {
        printf("Failed to load scene json: %s\n", e.what());
      } catch(...) {
        // ignore
      }

      try {
        int id = std::stoi(path.filename().string());
        entries.push_back({id, name, relPath});
      } catch(...) {
        // ignore
      }
    }
  }

  // sort by id
  std::ranges::sort(entries, [](const SceneEntry &a, const SceneEntry &b) {
    return a.id < b.id;
  });
}

Project::SceneManager::~SceneManager() {
  delete loadedScene;
}

void Project::SceneManager::save() {
  if (loadedScene) {
    loadedScene->save();
    auto p = fs::path{getScenePath(project)} / std::to_string(loadedScene->getId()) / "scene.json";
    project->noteSelfWrite(p.string());
  }
}

void Project::SceneManager::add() {
  auto scenesPath = getScenePath(project);
  int newId = 1;
  for (const auto &entry : entries) {
    if (entry.id >= newId) {
      newId = entry.id + 1;
    }
  }
  auto newPath = fs::path{scenesPath} / std::to_string(newId);
  printf("Create-Scene: %s\n", newPath.c_str());
  fs::create_directory(newPath);

  reload();
}

void Project::SceneManager::remove(int id) {
  auto scenesPath = getScenePath(project);
  auto scenePath = fs::path{scenesPath} / std::to_string(id);

  if (loadedScene && loadedScene->getId() == id) {
    delete loadedScene;
    loadedScene = nullptr;
  }

  printf("Remove-Scene: %s\n", scenePath.c_str());
  fs::remove_all(scenePath);
  reload();

  if (!loadedScene && !entries.empty()) {
    loadScene(entries.front().id);
  }
}

void Project::SceneManager::duplicate(int id)
{
  auto scenesPath = getScenePath(project);
  auto scenePath = fs::path{scenesPath} / std::to_string(id);

  int newId = 1;
  for (const auto &entry : entries) {
    if (entry.id >= newId) {
      newId = entry.id + 1;
    }
  }
  auto newPath = fs::path{scenesPath} / std::to_string(newId);
  printf("Duplicate-Scene: %s -> %s\n", scenePath.c_str(), newPath.c_str());
  fs::copy(scenePath, newPath, fs::copy_options::recursive);

  reload();
}

void Project::SceneManager::loadScene(int id) {
  if (loadedScene) {
    loadedScene->save();
    auto p = fs::path{getScenePath(project)} / std::to_string(loadedScene->getId()) / "scene.json";
    project->noteSelfWrite(p.string());
    delete loadedScene;
    reload(); // ensure names are up to date in case the loaded scene was renamed
  }
  //if we load a scene we should clear the undo history
  Editor::UndoRedo::getHistory().clear();

  loadedScene = new Scene(id, project->getPath());
}

void Project::SceneManager::reloadFromDisk(int id)
{
  bool wasLoaded = (loadedScene && loadedScene->getId() == id);
  if (wasLoaded) {
    delete loadedScene;
    loadedScene = nullptr;
    Editor::UndoRedo::getHistory().clear();
    ctx.mainSelection.clear();
    loadedScene = new Scene(id, project->getPath());
  }
  // Refresh entries (names, relPaths) regardless, so the content browser
  // reflects any external rename.
  reload();
}

namespace
{
  // Treat path B as nested under path A iff B == A or B starts with A + '/'.
  bool isUnderOrEqual(const std::string &child, const std::string &parent) {
    if (parent.empty()) return true;
    if (child == parent) return true;
    return child.size() > parent.size()
        && child[parent.size()] == '/'
        && child.compare(0, parent.size(), parent) == 0;
  }
}

void Project::SceneManager::setSceneRelPath(int id, const std::string &newRelPath)
{
  auto scenesPath = getScenePath(project);
  auto sceneJsonPath = fs::path{scenesPath} / std::to_string(id) / "scene.json";

  if (loadedScene && loadedScene->getId() == id) {
    loadedScene->relPath = newRelPath;
    loadedScene->save();
    project->noteSelfWrite(sceneJsonPath.string());
  } else {
    if (!fs::exists(sceneJsonPath)) return;

    try {
      auto doc = Utils::JSON::loadFile(sceneJsonPath);
      if (!doc.is_object()) return;
      if (newRelPath.empty()) doc.erase("relPath");
      else                    doc["relPath"] = newRelPath;
      Utils::FS::saveTextFile(sceneJsonPath.string(), doc.dump(2));
      project->noteSelfWrite(sceneJsonPath.string());
    } catch (...) {
      return;
    }
  }

  for (auto &e : entries) {
    if (e.id == id) {
      e.relPath = newRelPath;
      break;
    }
  }
}

void Project::SceneManager::renameSceneFolder(const std::string &oldPrefix, const std::string &newPrefix)
{
  if (oldPrefix.empty()) return;
  for (const auto &entry : entries) {
    if (!isUnderOrEqual(entry.relPath, oldPrefix)) continue;
    std::string suffix = entry.relPath.substr(oldPrefix.size()); // "" or "/sub..."
    std::string updated = newPrefix + suffix;
    setSceneRelPath(entry.id, updated);
  }
}

std::vector<int> Project::SceneManager::findScenesUnder(const std::string &prefix) const
{
  std::vector<int> ids;
  if (prefix.empty()) return ids;
  for (const auto &e : entries) {
    if (isUnderOrEqual(e.relPath, prefix)) ids.push_back(e.id);
  }
  return ids;
}
