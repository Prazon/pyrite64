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
  class SceneGraph
  {
    private:

    public:
      // When true, each Object node lists its Component::Entry items inline
      // as children — matching UE5's Components panel where the actor's
      // attached components show in the hierarchy. Default false so the
      // main scene graph stays a pure object tree.
      bool showComponentsInline{false};

      // explicit scene + selection so the same widget drives the
      // active scene and any open PrefabEditor's prefab-as-scene wrapper.
      void draw(Project::Scene &scene, Project::Selection &selection);
  };
}
