/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once

#include <vector>

namespace Project {
  class Scene;
  class Object;
  class Selection;
}

namespace Editor::SelectionUtils
{
  // SPBF64 fork: take an explicit Selection& so the same util works for the
  // active scene and for prefab-editor scenes.
  std::vector<Project::Object*> collectSelectedObjects(Project::Scene &scene, const Project::Selection &selection);

  bool deleteSelectedObjects(Project::Scene &scene, Project::Selection &selection);
}
