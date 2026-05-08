/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>

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

      // When true, skip the synthetic Scene wrapper and render the prefab
      // root (root.children[0]) as the topmost item, badged with the
      // prefab icon. Set by PrefabEditor; off in the main scene graph
      // where the wrapper exists for actual reasons (multiple top-level
      // objects + scene-wide settings).
      bool prefabRootMode{false};

      // Filled in by drawObjectNode whenever the user right-clicks an
      // object node inside a prefab editor and picks "Make Root". Polled
      // by PrefabEditor::draw after the SceneGraph runs and consumed
      // through Project::Scene::reparentAsRoot so the structural mutation
      // happens outside of the in-tree iteration.
      uint32_t pendingPromoteToRoot{0};

      // Mirrors pendingPromoteToRoot but for component-leaf clicks. Carries
      // the (object UUID, component UUID) of the row the user just selected
      // in inline-component mode. PrefabEditor consumes this to drive its
      // single-component details view; cleared after read so subsequent
      // frames don't keep re-asserting the same selection.
      uint32_t pendingComponentObjUUID{0};
      uint64_t pendingComponentUUID{0};
      // The component UUID currently shown as selected (highlight) in the
      // graph. Owned by the host (PrefabEditor) but read here per-frame.
      uint64_t selectedComponentUUID{0};

      // explicit scene + selection so the same widget drives the
      // active scene and any open PrefabEditor's prefab-as-scene wrapper.
      void draw(Project::Scene &scene, Project::Selection &selection);
  };
}
