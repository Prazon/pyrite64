/**
* @copyright 2025
* @license MIT
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../scene/scene.h"
#include "../../scene/sceneManager.h"
#include "../../../editor/undoRedo.h"

namespace
{
  // Mirrors the editor TABLE id for the Path component.
  constexpr int COMPONENT_ID_PATH = 18;

  constexpr uint32_t MODE_ONCE     = 0;
  constexpr uint32_t MODE_LOOP     = 1;
  constexpr uint32_t MODE_PINGPONG = 2;

  // Find the first Path component entry on an editor object.
  Project::Component::Entry* findPathEntry(Project::Object *o)
  {
    if (!o) return nullptr;
    for (auto &e : o->components) {
      if (e.id == COMPONENT_ID_PATH) return &e;
    }
    return nullptr;
  }
}

namespace Project::Component::PathFollow
{
  struct Data
  {
    PROP_U32(objectUUID);      // explicit Path owner; 0 = auto (self/parent)
    PROP_FLOAT(speed);         // units / second
    PROP_FLOAT(startDistance); // initial arc length at runtime
    PROP_S32(mode);            // 0 Once, 1 Loop, 2 PingPong
    PROP_BOOL(orient);         // also drive obj.rot from the path frame
    PROP_BOOL(autoPlay);       // start moving at scene load
    PROP_FLOAT(previewDistance); // editor-only PiP scrubber (not sent to engine)
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->speed.value = 200.0f;
    data->mode.value = (int32_t)MODE_ONCE;
    data->orient.value = true;
    data->autoPlay.value = true;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    return Utils::JSON::Builder{}
      .set(data.objectUUID)
      .set(data.speed)
      .set(data.startDistance)
      .set(data.mode)
      .set(data.orient)
      .set(data.autoPlay)
      .set(data.previewDistance)
      .doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->objectUUID);
    Utils::JSON::readProp(doc, data->speed);
    Utils::JSON::readProp(doc, data->startDistance);
    Utils::JSON::readProp(doc, data->mode);
    Utils::JSON::readProp(doc, data->orient);
    Utils::JSON::readProp(doc, data->autoPlay);
    Utils::JSON::readProp(doc, data->previewDistance);
    return data;
  }

  // Binary layout MUST match engine pathFollow.cpp::InitData (packed, 12B):
  //   uint16 refObjId; uint8 mode; uint8 flags; float speed; float startDist;
  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    auto objRef = ctx.scene ? ctx.scene->getObjectByUUID(data.objectUUID.value) : nullptr;
    uint16_t refObjId = objRef ? (uint16_t)objRef->runtimeId : 0;

    uint8_t flags = 0;
    if (data.orient.resolve(obj))   flags |= 1 << 0;
    if (data.autoPlay.resolve(obj)) flags |= 1 << 1;

    ctx.fileObj.write<uint16_t>(refObjId);
    ctx.fileObj.write<uint8_t>((uint8_t)data.mode.resolve(obj));
    ctx.fileObj.write<uint8_t>(flags);
    ctx.fileObj.write<float>(data.speed.resolve(obj));
    ctx.fileObj.write<float>(data.startDistance.resolve(obj));
  }

  // Resolve the followed Path: this object, then parent, then the explicit
  // reference object. Mirrors the engine resolution order.
  static bool resolvePath(Object &followObj, Data &data,
                          Object *&outPathObj, Entry *&outPathEntry)
  {
    if (auto *e = findPathEntry(&followObj)) {
      outPathObj = &followObj; outPathEntry = e; return true;
    }
    if (auto *e = findPathEntry(followObj.parent)) {
      outPathObj = followObj.parent; outPathEntry = e; return true;
    }
    if (data.objectUUID.value && ctx.project) {
      if (auto *sc = ctx.project->getScenes().getLoadedScene()) {
        if (auto sp = sc->getObjectByUUID(data.objectUUID.value)) {
          if (auto *e = findPathEntry(sp.get())) {
            outPathObj = sp.get(); outPathEntry = e; return true;
          }
        }
      }
    }
    return false;
  }

  // Shared with the viewport PiP preview: sample the resolved path at the
  // inspector scrubber distance and return the follower's world frame.
  bool previewFollowerFrame(Object &followObj, Path::SampleFrame &out)
  {
    for (auto &e : followObj.components) {
      if (e.id != 31) continue; // PathFollow TABLE id
      Data &data = *static_cast<Data*>(e.data.get());
      Object *pathObj = nullptr; Entry *pathEntry = nullptr;
      if (!resolvePath(followObj, data, pathObj, pathEntry)) return false;
      return Path::sampleAtDistance(*pathObj, *pathEntry,
                                    data.previewDistance.value, 0, out);
    }
    return false;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    // Resolve once for the preview-distance slider range + status line.
    Path::SampleFrame frame{};
    Object *pathObj = nullptr; Entry *pathEntry = nullptr;
    bool havePath = resolvePath(obj, data, pathObj, pathEntry);
    float pathLen = 0.0f;
    if (havePath && Path::sampleAtDistance(*pathObj, *pathEntry, 0.0f, 0, frame)) {
      pathLen = frame.totalLength;
    }

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      std::vector<ImTable::ComboEntry> modeList{
        {MODE_ONCE,     "Once (stop at end)"},
        {MODE_LOOP,     "Loop"},
        {MODE_PINGPONG, "Ping-Pong"},
      };
      ImTable::addObjProp<int32_t>("End Mode", data.mode, [&modeList](int32_t *val) -> bool {
        uint32_t proxy = (uint32_t)*val;
        ImGui::VectorComboBox("##", modeList, proxy);
        if ((int32_t)proxy == *val) return false;
        *val = (int32_t)proxy;
        return true;
      }, nullptr);

      // Path source: <Auto> resolves self -> parent; otherwise an explicit
      // object that carries a Path component.
      auto &map = ctx.project->getScenes().getLoadedScene()->objectsMap;
      std::vector<ImTable::ComboEntry> objList;
      objList.push_back({0, "<Auto (self / parent)>"});
      for (auto &[uuid, object] : map) {
        if (findPathEntry(object.get()))
          objList.push_back({ .value = object->uuid, .name = object->name });
      }
      ImTable::addObjProp<uint32_t>("Path Source", data.objectUUID, [&objList](uint32_t *val) -> bool {
        uint32_t proxy = *val;
        ImGui::VectorComboBox("##", objList, proxy);
        if (proxy == *val) return false;
        *val = proxy;
        return true;
      }, nullptr);

      ImTable::addObjProp("Speed",       data.speed);
      ImTable::addObjProp("Start Dist.", data.startDistance);
      ImTable::addObjProp("Orient to Path", data.orient);
      ImTable::addObjProp("Auto-Play",      data.autoPlay);

      // Editor-only scrubber. Drives the camera PiP preview when this
      // object (or a child) has a Camera. Not written to the engine.
      ImTable::add("Preview Dist.");
      if (havePath && pathLen > 0.0f) {
        float pv = data.previewDistance.value;
        if (pv > pathLen) pv = pathLen;
        if (ImGui::SliderFloat("##previewDist", &pv, 0.0f, pathLen, "%.1f")) {
          data.previewDistance.value = pv;
          Editor::UndoRedo::getHistory().markChanged("PathFollow: preview");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("/ %.1f", pathLen);
      } else {
        ImGui::TextDisabled("(no Path resolved)");
      }

      ImTable::end();
    }
  }
}
