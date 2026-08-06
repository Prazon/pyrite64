/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "object.h"

namespace Project
{
  struct LayerConf
  {
    PROP_STRING(name);
    PROP_BOOL(depthCompare);
    PROP_BOOL(depthWrite);
    PROP_U32(blender);
    PROP_BOOL(fog);
    PROP_U32(fogColorMode);
    PROP_VEC4(fogColor);
    PROP_FLOAT(fogMin);
    PROP_FLOAT(fogMax);
    PROP_S32(lightMode);
  };

  struct SceneConf
  {
    PROP_STRING(name);
    int fbWidth{320};
    int fbHeight{240};
    int fbFormat{0};
    PROP_VEC4(clearColor);
    PROP_BOOL(doClearColor);
    PROP_BOOL(doClearDepth);
    PROP_S32(renderPipeline);
    // 0 = Mode3D (default, runs full 3D pass with optional 2D overlay),
    // 1 = Mode2D (skips the 3D pass entirely; pure-2D scenes only walk
    // RENDER_LAYER_2D objects). Persisted as a single byte at engine load
    // time, defaulting to 0 so existing scenes are unaffected.
    PROP_S32(renderMode);
    PROP_S32(frameLimit);
    PROP_S32(filter);
    PROP_S32(audioFreq);
    PROP_S32(physicsTickRate);
    PROP_VEC3(gravity);
    PROP_FLOAT(visualUnitsPerMeter);
    PROP_S32(velocitySolverIterations);
    PROP_S32(positionSolverIterations);
    PROP_BOOL(interpolatePhysicsTransforms);

    std::vector<LayerConf> layers3D{};
    std::vector<LayerConf> layersPtx{};
    std::vector<LayerConf> layers2D{};

    nlohmann::json serialize() const;
  };

  class Scene
  {
    private:
      int id{};
      Object root{};
      std::string scenePath{};

    public:
      SceneConf conf{};

      // Virtual content-browser folder this scene appears in. Empty = the
      // root of the unified Content view. Persisted as a top-level field in
      // scene.json so it stays out of SceneConf (the on-device baked image)
      // and never reaches the runtime.
      std::string relPath{};

      Scene(int id_, const std::string &projectPath);

      // SPBF64 fork: in-memory scene with no disk backing (no scene.json).
      // Used by PrefabEditor to host a prefab's Object subtree as a Scene so
      // SceneGraph / ObjectInspector can drive it through their normal API.
      Scene();

      int getId() const { return id; }
      const std::string &getName() const { return conf.name.value; }

      void save();
      Object& getRootObject() { return root; }

      // SPBF64 fork: load a single Object subtree from JSON as the only child
      // of root. Clears any existing objects first.
      void loadFromObjectJSON(const std::string &objJson);

      // SPBF64 fork: serialize root's first child to JSON. Used to persist the
      // prefab subtree back to disk on save. Returns "{}" if there is no
      // first child.
      std::string serializeRootChild() const;

      std::unordered_map<uint32_t, std::shared_ptr<Object>> objectsMap{};

      std::shared_ptr<Object> addObject(std::string &objJson, uint64_t parentUUID = 0);
      std::shared_ptr<Object> addObject(Object &parent);
      std::shared_ptr<Object> addObject(Object &parent, std::shared_ptr<Object> obj, bool generateUUID = false);


      /**
       * Creates an object with a static or animated Model component for a 3D model asset.
       * @param modelUUID UUID of the 3D model asset.
       * @return Created scene object, or null when the asset is not a 3D model.
       */
      std::shared_ptr<Object> addModelObject(uint64_t modelUUID);

      void removeObject(Object &obj);
      void removeAllObjects();

      bool moveObject(uint32_t uuidObject, uint32_t uuidTarget, bool asChild);

      // Promote `uuidNewRoot` to be the prefab's root (root.children[0]).
      // The previous root becomes a child of the new root, preserving its
      // remaining subtree. Used by PrefabEditor's "Make Root" action.
      bool promoteToPrefabRoot(uint32_t uuidNewRoot);

      std::shared_ptr<Object> getObjectByUUID(uint32_t uuid) {
        if (objectsMap.contains(uuid)) {
          return objectsMap[uuid];
        }
        return nullptr;
      }

      // Spawn a prefab instance under `parent` (or scene root when nullptr).
      // Used by viewport / scene-graph drag-drop and the prefab right-click
      // shortcuts; the parented form is what makes drag-drop into a specific
      // hierarchy node behave like Unity / Godot.
      std::shared_ptr<Object> addPrefabInstance(uint64_t prefabUUID, Object *parent = nullptr);

      uint64_t createPrefabFromObject(uint32_t uuid, const std::string &subDir = {});

      // Unpacks a prefab instance (shallow) into real, editable scene objects
      void unpackPrefabInstance(uint32_t uuid);

      // Re-materializes the fromPrefab subtree under every instance of the
      // given prefab uuid. User-added (fromPrefab=false) children of an
      // instance are preserved; their fromPrefab descendants are dropped and
      // rebuilt from the prefab's current children. Stale selection on
      // dropped nodes is harmless (lookups return nullptr). No-op if the
      // prefab is missing.
      void refreshPrefabInstances(uint64_t prefabUUID);

      std::string serialize(bool minify = false);

      void resetLayers();

      void deserialize(const std::string &data);

      // Assigns the runtime object ids (uint16_t) for the whole tree.
      // Build-time only: must be called before serializing objects to the runtime format.
      // Returns the first free id (base for build-time expanded prefab-instance children).
      uint32_t assignRuntimeIds();
  };
}
