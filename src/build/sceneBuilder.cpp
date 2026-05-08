/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectBuilder.h"
#include "../utils/string.h"
#include <algorithm>
#include <cstring>
#include <filesystem>

#include "../utils/binaryFile.h"
#include "../utils/fs.h"
#include "../utils/logger.h"

#include "engine/include/scene/objectFlags.h"

namespace T3D
{
 #include "tiny3d/tools/gltf_importer/src/math/quantizer.h"
}

namespace fs = std::filesystem;

namespace
{
  constexpr uint32_t FLAG_CLR_DEPTH = 1 << 0;
  constexpr uint32_t FLAG_CLR_COLOR = 1 << 1;
  constexpr uint32_t FLAG_SCR_32BIT = 1 << 2;
}

uint32_t Build::writeObject(Build::SceneCtx &ctx, Project::Object &obj, bool savePrefabItself)
{
  auto srcObj = &obj;
  std::shared_ptr<Project::Prefab> prefab{};
  if(!savePrefabItself && obj.isPrefabInstance())
  {
    prefab = ctx.project->getAssets().getPrefabByUUID(srcObj->uuidPrefab.value);
    if(prefab)srcObj = &prefab->obj;
  }

  // Prefab-instance objects with class variables get a trailing fixed-size
  // variable block written after the component terminator. The flag below
  // marks that block so the runtime reader can skip/consume it without
  // breaking older scenes that don't have it.
  const bool hasVars = !savePrefabItself && prefab && !prefab->variables.empty();

  uint16_t objFlags = 0;
  if(obj.enabled)objFlags |= P64::ObjectFlags::ACTIVE;
  if(!obj.children.empty())objFlags |= P64::ObjectFlags::HAS_CHILDREN;
  if(hasVars)objFlags |= P64::ObjectFlags::HAS_PREFAB_VARS;

  ctx.fileObj.write<uint16_t>(objFlags); // @TODO type
  ctx.fileObj.write<uint16_t>(obj.id);
  ctx.fileObj.write<uint16_t>(obj.parent ? obj.parent->id : 0);
  ctx.fileObj.write<uint16_t>(0); // padding
  ctx.fileObj.write(srcObj->pos.resolve(obj.propOverrides));
  ctx.fileObj.write(srcObj->scale.resolve(obj.propOverrides));

  auto &rot = srcObj->rot.resolve(obj.propOverrides);
  uint32_t quatQuant = T3D::Quantizer::quatTo32Bit({rot.x, rot.y, rot.z, rot.w});
  ctx.fileObj.write(quatQuant);

  // DATA
  auto saveComp = [&ctx, &obj](Project::Component::Entry &comp) {
    auto compPos = ctx.fileObj.getPos();
    ctx.fileObj.skip(2);
    ctx.fileObj.skip(2); // flags (@TODO)

    if (comp.id >= 0 && comp.id < Project::Component::TABLE.size()) {
      Project::Component::TABLE[comp.id].funcBuild(obj, comp, ctx);
    } else {
      Utils::Logger::log("Component ID not found: " + std::to_string(comp.id), Utils::Logger::LEVEL_ERROR);
      assert(false);
    }

    ctx.fileObj.align(4);
    auto size = (ctx.fileObj.getPos() - compPos) / 4;
    assert(size < 256);

    ctx.fileObj.posPush(compPos);
    ctx.fileObj.write<uint8_t>(comp.id);
    ctx.fileObj.write<uint8_t>(size);
    ctx.fileObj.posPop();
    //ctx.fileObj.write<uint16_t>(comp.id);
  };


  std::vector<Project::Component::Entry*> compList{};
  for (auto &comp : srcObj->components) {
    compList.push_back(&comp);
  }

  if(srcObj != &obj) {
    for (auto &comp : obj.components) {
      compList.push_back(&comp);
    }
  }

  // sort by component prio
  std::stable_sort(compList.begin(), compList.end(),
    [](const Project::Component::Entry* a, const Project::Component::Entry* b) {
      int prioA = Project::Component::TABLE[a->id].prio;
      int prioB = Project::Component::TABLE[b->id].prio;
      return prioA < prioB;
    }
  );

  for(auto &comp : compList) {
    saveComp(*comp);
  }

  ctx.fileObj.write<uint32_t>(0);

  if (hasVars) {
    // Fixed-size variable record: keeps the runtime reader trivial and lets
    // the engine fast-path skip the block when prefab actors aren't wired up.
    //   uuid  : uint64                                 (8B)
    //   kind  : uint8                                  (1B)
    //   pad   : uint8[3]                               (3B)
    //   value : uint8[20] (covers vec3=12B and quat=16B; primitives <= 8B)
    // -> 32 bytes per variable. varCount fits in a uint16; align to 4 bytes.
    constexpr int VAR_RECORD_BYTES = 32;
    const auto &vars = prefab->variables;
    ctx.fileObj.write<uint16_t>(static_cast<uint16_t>(vars.size()));
    ctx.fileObj.write<uint16_t>(0); // padding to 4-byte align value blocks

    for (const auto &v : vars) {
      // Resolve effective value: instance override if present, otherwise the
      // prefab class default. Both end up baked into the scene as plain bytes
      // — the runtime never reads them as `GenericValue`.
      GenericValue val = v.defaultValue;
      auto it = obj.varOverrides.find(v.uuid);
      if (it != obj.varOverrides.end()) val = it->second;

      ctx.fileObj.write<uint64_t>(v.uuid);
      ctx.fileObj.write<uint8_t>(static_cast<uint8_t>(v.kind));
      ctx.fileObj.write<uint8_t>(0);
      ctx.fileObj.write<uint8_t>(0);
      ctx.fileObj.write<uint8_t>(0);

      // Layout the value bytes into a 20-byte buffer; unused tail is zeroed.
      // Type widths must match prefabBuilder.cpp's kindToType so generated
      // PODs read the same bytes.
      uint8_t valBuf[20]{};
      switch (v.kind) {
        case Project::PrefabVarKind::INT: {
          int32_t x = val.get<int32_t>();
          std::memcpy(valBuf, &x, sizeof(x));
          break;
        }
        case Project::PrefabVarKind::FLOAT: {
          float x = val.get<float>();
          std::memcpy(valBuf, &x, sizeof(x));
          break;
        }
        case Project::PrefabVarKind::BOOL: {
          uint8_t x = val.get<bool>() ? 1 : 0;
          valBuf[0] = x;
          break;
        }
        case Project::PrefabVarKind::VEC3: {
          glm::vec3 vec = val.get<glm::vec3>();
          float xs[3]{vec.x, vec.y, vec.z};
          std::memcpy(valBuf, xs, sizeof(xs));
          break;
        }
        case Project::PrefabVarKind::QUAT: {
          glm::quat q = val.get<glm::quat>();
          float xs[4]{q.x, q.y, q.z, q.w};
          std::memcpy(valBuf, xs, sizeof(xs));
          break;
        }
        case Project::PrefabVarKind::OBJECT_REF:
        case Project::PrefabVarKind::PREFAB_REF:
        case Project::PrefabVarKind::ASSET_REF: {
          uint64_t x = val.get<uint64_t>();
          std::memcpy(valBuf, &x, sizeof(x));
          break;
        }
      }
      for (int i = 0; i < 20; ++i) ctx.fileObj.write<uint8_t>(valBuf[i]);
    }
    static_assert(VAR_RECORD_BYTES == 8 + 4 + 20,
      "variable record layout drifted from runtime expectation");
  }

  uint32_t count = 1;
  for (const auto &child : obj.children) {
    count += writeObject(ctx, *child, savePrefabItself);
  }
  return count;
}

void Build::buildScene(Project::Project &project, const Project::SceneEntry &scene, SceneCtx &ctx)
{
  std::string fileNameScene = "s" + Utils::padLeft(std::to_string(scene.id), '0', 4);
  std::string fileNameObj = fileNameScene + "o";

  std::unique_ptr<Project::Scene> sc{new Project::Scene(scene.id, project.getPath())};
  ctx.scene = sc.get();

  auto fsDataPath = fs::absolute(fs::path{project.getPath()} / "filesystem" / "p64");

  uint32_t sceneFlags = 0;
  uint32_t objCountExpected = sc->objectsMap.size();
  uint32_t objCount = 0;

  if (sc->conf.doClearDepth.value)sceneFlags |= FLAG_CLR_DEPTH;
  if (sc->conf.doClearColor.value)sceneFlags |= FLAG_CLR_COLOR;
  if (sc->conf.fbFormat)sceneFlags |= FLAG_SCR_32BIT;

  ctx.fileObj = {};
  auto &rootObj = sc->getRootObject();
  for (const auto &child : rootObj.children) {
    objCount += writeObject(ctx, *child, false);
  }

  ctx.fileObj.writeToFile(fsDataPath / fileNameObj);

  ctx.fileScene = {};
  ctx.fileScene.write<uint16_t>(sc->conf.fbWidth);
  ctx.fileScene.write<uint16_t>(sc->conf.fbHeight);
  ctx.fileScene.write(sceneFlags);
  ctx.fileScene.writeRGBA(sc->conf.clearColor.value);
  ctx.fileScene.write(objCount);

  ctx.fileScene.write<uint8_t>(sc->conf.renderPipeline.value);
  ctx.fileScene.write<uint8_t>(sc->conf.frameLimit.value);
  ctx.fileScene.write<uint8_t>(sc->conf.filter.value);
  ctx.fileScene.write<uint8_t>(0); // padding

  ctx.fileScene.write<uint16_t>(sc->conf.audioFreq.value);
  ctx.fileScene.write<uint16_t>(std::clamp(sc->conf.physicsTickRate.value, 1, 100));

  const auto &gravity = sc->conf.gravity.value;
  ctx.fileScene.write<float>(gravity.x);
  ctx.fileScene.write<float>(gravity.y);
  ctx.fileScene.write<float>(gravity.z);
  ctx.fileScene.write<float>(std::max(sc->conf.visualUnitsPerMeter.value, 0.001f));

  ctx.fileScene.write<uint8_t>(std::clamp(sc->conf.velocitySolverIterations.value, 1, 32));
  ctx.fileScene.write<uint8_t>(std::clamp(sc->conf.positionSolverIterations.value, 1, 32));
  ctx.fileScene.write<uint8_t>(sc->conf.interpolatePhysicsTransforms.value ? 1 : 0);
  ctx.fileScene.write<uint8_t>(0); // padding

  // Layer::Setup
  ctx.fileScene.write<uint8_t>(sc->conf.layers3D.size());
  ctx.fileScene.write<uint8_t>(sc->conf.layersPtx.size());
  ctx.fileScene.write<uint8_t>(sc->conf.layers2D.size());
  ctx.fileScene.write<uint8_t>(0); // padding

  auto writeLayer = [&ctx](const Project::LayerConf &layer) {
    uint32_t flags = 0;
    if(layer.depthWrite.value)flags |= (1 << 0);
    if(layer.depthCompare.value)flags |= (1 << 1);

    ctx.fileScene.write<uint32_t>(flags);
    ctx.fileScene.write<uint32_t>(layer.blender.value);

    uint8_t fogMode = layer.fog.value ? layer.fogColorMode.value : 0;
    ctx.fileScene.writeRGBA(layer.fogColor.value);
    ctx.fileScene.write<float>(layer.fogMin.value);
    ctx.fileScene.write<float>(layer.fogMax.value);
    ctx.fileScene.write<uint8_t>(fogMode);
    ctx.fileScene.write<uint8_t>(layer.lightMode.value);
    ctx.fileScene.write<uint8_t>(0); // padding
    ctx.fileScene.write<uint8_t>(0); // padding
  };

  for(const auto &layer : sc->conf.layers3D)writeLayer(layer);
  for(const auto &layer : sc->conf.layersPtx)writeLayer(layer);
  for(const auto &layer : sc->conf.layers2D)writeLayer(layer);

  ctx.fileScene.align(4);
  ctx.fileScene.writeToFile(fsDataPath / fileNameScene);

  ctx.files.push_back("filesystem/p64/" + fileNameScene);
  ctx.files.push_back("filesystem/p64/" + fileNameObj);

  ctx.scene = nullptr;
}
