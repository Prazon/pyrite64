/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once

namespace Project
{
  class Scene;
  class Selection;
}

namespace Editor
{
  class ObjectInspector
  {
    private:

    public:
      ObjectInspector();

      // SPBF64 fork: explicit scene + selection so the inspector drives the
      // active scene and any open PrefabEditor's prefab-as-scene wrapper.
      void draw(Project::Scene &scene, Project::Selection &selection);
  };
}
