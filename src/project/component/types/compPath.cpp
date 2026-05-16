/**
* Editor-side Path component.
* Pairs with engine: n64/engine/src/scene/components/path.cpp
*
* Authors a Catmull-Rom spline through control points placed in the owning
* Object's local space, with optional branch nodes that fork the path at
* fork points. Authoring is done in the 3D scene viewport; this file holds
* only the schema, JSON round-trip, build-time blob layout, and the
* inspector / viewport hooks.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../editor/undoRedo.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../../utils/colors.h"
#include "../../../utils/meshGen.h"
#include "../../assetManager.h"
#include "../../../editor/pages/parts/viewport3D.h"

#include "imgui.h"
#include "ImGuizmo.h"
#include "glm/vec3.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "glm/ext/matrix_projection.hpp"

#include <unordered_map>
#include <vector>
#include <string>
#include <cmath>

namespace Project::Component::Path
{
  // Mirrors the engine layout (engine path.h: P64::Comp::Path::CtrlPoint).
  struct CtrlPoint
  {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    float     tension{0.5f};
    uint8_t   branchId{0};
    uint8_t   flags{0};
  };

  struct Branch
  {
    uint16_t    fromIdx{0};
    uint8_t     branchId{1};
    uint8_t     op{4};            // 0=eq 1=ne 2=lt 3=le 4=gt 5=ge
    std::string flagName{};       // editor stores name; build hashes to flagId
    float       value{0.0f};
  };

  struct Data
  {
    std::vector<CtrlPoint> points;
    std::vector<Branch>    branches;
    PROP_S32(lutPerSegment);    // sample density used by the engine LUT (8-32)
    PROP_FLOAT(tPreview);       // scrubber for ghost-frame preview in viewport
    PROP_S32(previewGroup);     // 0 = trunk; 1..7 = preview that branch group
  };

  // Transient per-entry UI state (which control point is "sub-selected" for
  // gizmo manipulation in the viewport). Keyed by entry uuid so it survives
  // panel re-layouts but does not round-trip through the saved scene.
  static std::unordered_map<uint64_t, int> g_selectedPointByEntry{};
  int& selectedPointFor(uint64_t entryUuid) { return g_selectedPointByEntry[entryUuid]; }

  // FNV-1a 16-bit (matches engine's PathRT::hashFlag in n64/engine/include/scene/path.h).
  static uint16_t hashFlag(const std::string &name)
  {
    uint32_t h = 2166136261u;
    for (unsigned char c : name) { h ^= c; h *= 16777619u; }
    return (uint16_t)((h ^ (h >> 16)) & 0xFFFFu);
  }

  // Forward decl: defined later in this TU but referenced from the inspector.
  static std::vector<int> usedGroups(const Data &data);

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->lutPerSegment.value = 12;
    data->tPreview.value = 0.0f;
    data->previewGroup.value = 0;
    // Seed with two points so a freshly-added Path has a visible spline to grab.
    data->points.push_back(CtrlPoint{ {0.0f, 0.0f, 0.0f}, 0.5f, 0, 0 });
    data->points.push_back(CtrlPoint{ {0.0f, 0.0f, 100.0f}, 0.5f, 0, 0 });
    return data;
  }

  // ---- JSON round-trip ----------------------------------------------------

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.lutPerSegment);
    b.set(data.tPreview);
    b.set(data.previewGroup);

    nlohmann::json pts = nlohmann::json::array();
    for (auto &p : data.points) {
      pts.push_back({
        {"pos",      {p.pos.x, p.pos.y, p.pos.z}},
        {"tension",  p.tension},
        {"branchId", p.branchId},
        {"flags",    p.flags},
      });
    }
    b.doc["points"] = std::move(pts);

    nlohmann::json brs = nlohmann::json::array();
    for (auto &br : data.branches) {
      brs.push_back({
        {"fromIdx",  br.fromIdx},
        {"branchId", br.branchId},
        {"op",       br.op},
        {"flag",     br.flagName},
        {"value",    br.value},
      });
    }
    b.doc["branches"] = std::move(brs);

    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->lutPerSegment, 12);
    Utils::JSON::readProp(doc, data->tPreview, 0.0f);
    Utils::JSON::readProp(doc, data->previewGroup, 0);

    if (doc.contains("points") && doc["points"].is_array()) {
      for (auto &p : doc["points"]) {
        CtrlPoint cp{};
        if (p.contains("pos") && p["pos"].is_array() && p["pos"].size() == 3) {
          cp.pos.x = p["pos"][0].get<float>();
          cp.pos.y = p["pos"][1].get<float>();
          cp.pos.z = p["pos"][2].get<float>();
        }
        if (p.contains("tension"))  cp.tension  = p["tension"].get<float>();
        if (p.contains("branchId")) cp.branchId = p["branchId"].get<uint8_t>();
        if (p.contains("flags"))    cp.flags    = p["flags"].get<uint8_t>();
        data->points.push_back(cp);
      }
    }
    if (doc.contains("branches") && doc["branches"].is_array()) {
      for (auto &b : doc["branches"]) {
        Branch br{};
        if (b.contains("fromIdx"))  br.fromIdx  = b["fromIdx"].get<uint16_t>();
        if (b.contains("branchId")) br.branchId = b["branchId"].get<uint8_t>();
        if (b.contains("op"))       br.op       = b["op"].get<uint8_t>();
        if (b.contains("flag"))     br.flagName = b["flag"].get<std::string>();
        if (b.contains("value"))    br.value    = b["value"].get<float>();
        data->branches.push_back(br);
      }
    }
    return data;
  }

  // ---- Build (binary blob to engine) --------------------------------------

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    (void)obj;

    auto &fb = ctx.fileObj;

    // InitHeader (8 bytes): pointCount, branchCount, lutPerSegment, _pad
    int lutPerSeg = data.lutPerSegment.resolve(obj);
    if (lutPerSeg <= 0)  lutPerSeg = 12;
    if (lutPerSeg > 64)  lutPerSeg = 64;

    fb.write<uint16_t>((uint16_t)data.points.size());
    fb.write<uint16_t>((uint16_t)data.branches.size());
    fb.write<uint16_t>((uint16_t)lutPerSeg);
    fb.write<uint16_t>(0);

    // CtrlPointInit (20 bytes each)
    for (auto &p : data.points) {
      fb.write<float>(p.pos.x);
      fb.write<float>(p.pos.y);
      fb.write<float>(p.pos.z);
      fb.write<float>(p.tension);
      fb.write<uint8_t>(p.branchId);
      fb.write<uint8_t>(p.flags);
      fb.write<uint8_t>(0);
      fb.write<uint8_t>(0);
    }

    // BranchInit (12 bytes each)
    for (auto &br : data.branches) {
      fb.write<uint16_t>(br.fromIdx);
      fb.write<uint8_t>(br.branchId);
      fb.write<uint8_t>(br.op);
      fb.write<uint16_t>(hashFlag(br.flagName));
      fb.write<uint16_t>(0);
      fb.write<float>(br.value);
    }
  }

  // ---- Inspector ----------------------------------------------------------

  static const char* kOpLabels[] = { "==", "!=", "<", "<=", ">", ">=" };

  // Forward decls for helpers used by both draw() and draw3D().
  static std::vector<int> usedGroups(const Data &data);

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int &selPt = selectedPointFor(entry.uuid);

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("LUT Density",   data.lutPerSegment);
      ImTable::addObjProp("Preview t",     data.tPreview);

      // "Preview Group" lets the designer scrub through alternative branch
      // routes in the viewport without changing runtime behavior.
      auto groups = usedGroups(data);
      if (!groups.empty()) {
        ImTable::add("Preview Group");
        int curr = data.previewGroup.value;
        char preview[24];
        std::snprintf(preview, sizeof(preview), "Group %d%s", curr,
                      curr == 0 ? " (trunk)" : "");
        if (ImGui::BeginCombo("##preview_group", preview)) {
          for (int g : groups) {
            char label[24];
            std::snprintf(label, sizeof(label), "Group %d%s", g,
                          g == 0 ? " (trunk)" : "");
            bool sel = (g == curr);
            if (ImGui::Selectable(label, sel)) {
              data.previewGroup.value = g;
              Editor::UndoRedo::getHistory().markChanged("Path: preview group");
            }
          }
          ImGui::EndCombo();
        }
      }

      ImTable::add("Points");
      ImGui::Text("%zu point%s", data.points.size(), data.points.size() == 1 ? "" : "s");
      ImGui::SameLine();
      if (ImGui::SmallButton(ICON_MDI_PLUS " Append")) {
        Editor::UndoRedo::getHistory().markChanged("Path: append point");
        glm::vec3 newPos{0.0f, 0.0f, 0.0f};
        if (!data.points.empty()) {
          newPos = data.points.back().pos + glm::vec3{0.0f, 0.0f, 100.0f};
        }
        data.points.push_back(CtrlPoint{newPos, 0.5f, 0, 0});
        selPt = (int)data.points.size() - 1;
      }
      ImTable::end();
    }

    // Per-point editor as a separate child region so it scrolls without
    // dragging the inspector around.
    if (ImGui::CollapsingHeader("Control Points", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::PushID("path_pts");
      for (int i = 0; i < (int)data.points.size(); ++i) {
        auto &p = data.points[i];
        ImGui::PushID(i);

        bool isSel = (selPt == i);
        ImGui::Bullet();
        ImGui::SameLine();
        if (ImGui::Selectable((std::string("Point ") + std::to_string(i)).c_str(), isSel,
                              ImGuiSelectableFlags_AllowOverlap)) {
          selPt = i;
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.0f);
        if (ImGui::SmallButton("Insert After")) {
          Editor::UndoRedo::getHistory().markChanged("Path: insert point");
          glm::vec3 mid = p.pos;
          if (i + 1 < (int)data.points.size()) mid = (p.pos + data.points[i + 1].pos) * 0.5f;
          else mid += glm::vec3{0.0f, 0.0f, 100.0f};
          data.points.insert(data.points.begin() + i + 1, CtrlPoint{mid, 0.5f, p.branchId, 0});
          ImGui::PopID();
          selPt = i + 1;
          break;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_DELETE)) {
          Editor::UndoRedo::getHistory().markChanged("Path: delete point");
          data.points.erase(data.points.begin() + i);
          if (selPt >= (int)data.points.size()) selPt = (int)data.points.size() - 1;
          ImGui::PopID();
          break;
        }

        if (isSel) {
          ImGui::Indent();
          if (ImGui::DragFloat3("Pos", &p.pos.x, 1.0f)) {
            Editor::UndoRedo::getHistory().markChanged("Path: move point");
          }
          if (ImGui::SliderFloat("Tension", &p.tension, 0.0f, 1.0f)) {
            Editor::UndoRedo::getHistory().markChanged("Path: tension");
          }
          int bid = p.branchId;
          if (ImGui::SliderInt("Branch Group", &bid, 0, 7)) {
            p.branchId = (uint8_t)bid;
            Editor::UndoRedo::getHistory().markChanged("Path: branchId");
          }
          ImGui::Unindent();
        }
        ImGui::PopID();
      }
      ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Branches")) {
      ImGui::PushID("path_brs");
      if (ImGui::SmallButton(ICON_MDI_PLUS " Add Branch")) {
        Editor::UndoRedo::getHistory().markChanged("Path: add branch");
        Branch br{};
        br.fromIdx = data.points.empty() ? 0 : (uint16_t)(data.points.size() / 2);
        br.branchId = 1;
        br.op = 4;
        br.value = 1.0f;
        data.branches.push_back(br);
      }
      for (int i = 0; i < (int)data.branches.size(); ++i) {
        auto &br = data.branches[i];
        ImGui::PushID(i);
        ImGui::Separator();
        ImGui::Text("Branch %d", i);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
        if (ImGui::SmallButton(ICON_MDI_DELETE)) {
          Editor::UndoRedo::getHistory().markChanged("Path: delete branch");
          data.branches.erase(data.branches.begin() + i);
          ImGui::PopID();
          break;
        }

        int from = br.fromIdx;
        int maxFrom = data.points.empty() ? 0 : (int)data.points.size() - 1;
        if (ImGui::SliderInt("From Point", &from, 0, maxFrom)) {
          br.fromIdx = (uint16_t)from;
          Editor::UndoRedo::getHistory().markChanged("Path: branch.from");
        }
        int target = br.branchId;
        if (ImGui::SliderInt("Take Group", &target, 1, 7)) {
          br.branchId = (uint8_t)target;
          Editor::UndoRedo::getHistory().markChanged("Path: branch.target");
        }

        char flagBuf[64];
        std::snprintf(flagBuf, sizeof(flagBuf), "%s", br.flagName.c_str());
        if (ImGui::InputText("Flag", flagBuf, sizeof(flagBuf))) {
          br.flagName = flagBuf;
          Editor::UndoRedo::getHistory().markChanged("Path: branch.flag");
        }

        int op = br.op;
        if (ImGui::Combo("Op", &op, kOpLabels, IM_ARRAYSIZE(kOpLabels))) {
          br.op = (uint8_t)op;
          Editor::UndoRedo::getHistory().markChanged("Path: branch.op");
        }
        if (ImGui::DragFloat("Value", &br.value, 0.1f)) {
          Editor::UndoRedo::getHistory().markChanged("Path: branch.value");
        }
        ImGui::PopID();
      }
      ImGui::PopID();
    }
  }

  // ---- Viewport authoring (in-3D-scene polyline + handles + ImGuizmo) ----

  // Catmull-Rom interpolation (uniform). Mirrors the engine side so the
  // editor-rendered polyline traces the exact curve the runtime will sample.
  static glm::vec3 catmullRom(const glm::vec3 &p0, const glm::vec3 &p1,
                              const glm::vec3 &p2, const glm::vec3 &p3,
                              float t, float tension)
  {
    float t2 = t * t;
    float t3 = t2 * t;
    glm::vec3 r{};
    for (int i = 0; i < 3; ++i) {
      float a = -tension * p0[i] + (2.0f - tension) * p1[i]
              + (tension - 2.0f) * p2[i] + tension * p3[i];
      float b = 2.0f * tension * p0[i] + (tension - 3.0f) * p1[i]
              + (3.0f - 2.0f * tension) * p2[i] - tension * p3[i];
      float c = -tension * p0[i] + tension * p2[i];
      float d = p1[i];
      r[i] = a * t3 + b * t2 + c * t + d;
    }
    return r;
  }

  // Collect indices into data.points belonging to the requested branch group,
  // preserving authoring order. Returned vector size < 2 means "no curve".
  static std::vector<int> groupIndices(const Data &data, int branchId)
  {
    std::vector<int> out;
    out.reserve(data.points.size());
    for (int i = 0; i < (int)data.points.size(); ++i) {
      if (data.points[i].branchId == (uint8_t)branchId) out.push_back(i);
    }
    return out;
  }

  // Walk the (possibly filtered) spline densely and call
  // cb(world_pos, arc_length_so_far) for each sample. Used for both the
  // polyline render and the scrubber lookup.
  template<typename CB>
  static void walkSpline(const Data &data, const glm::mat4 &objMat,
                         int samplesPerSeg, int branchId, CB cb)
  {
    auto idxs = groupIndices(data, branchId);
    if (idxs.size() < 2) return;
    auto getCtrl = [&](int i) -> glm::vec3 {
      if (i < 0) return data.points[idxs.front()].pos;
      if (i >= (int)idxs.size()) return data.points[idxs.back()].pos;
      return data.points[idxs[i]].pos;
    };

    glm::vec3 prevWorld{};
    bool havePrev = false;
    float arc = 0.0f;
    int segCount = (int)idxs.size() - 1;
    for (int s = 0; s < segCount; ++s) {
      glm::vec3 p0 = getCtrl(s - 1);
      glm::vec3 p1 = getCtrl(s);
      glm::vec3 p2 = getCtrl(s + 1);
      glm::vec3 p3 = getCtrl(s + 2);
      float tension = data.points[idxs[s]].tension;
      if (tension <= 0.0f) tension = 0.5f;

      int kEnd = (s == segCount - 1) ? samplesPerSeg : (samplesPerSeg - 1);
      for (int k = 0; k <= kEnd; ++k) {
        float t = (float)k / (float)samplesPerSeg;
        glm::vec3 local = catmullRom(p0, p1, p2, p3, t, tension);
        glm::vec3 world = glm::vec3(objMat * glm::vec4(local, 1.0f));
        if (havePrev) arc += glm::length(world - prevWorld);
        cb(world, arc);
        prevWorld = world;
        havePrev = true;
      }
    }
  }

  // Build the set of branchIds that have at least 2 points (and thus form
  // a real curve). Used for the inspector dropdown and for per-group polylines.
  static std::vector<int> usedGroups(const Data &data)
  {
    std::vector<int> out;
    int counts[8] = {0,0,0,0,0,0,0,0};
    for (auto &p : data.points) {
      if (p.branchId < 8) counts[p.branchId]++;
    }
    for (int b = 0; b < 8; ++b) if (counts[b] >= 2) out.push_back(b);
    return out;
  }

  static glm::mat4 objWorldMat(Object &obj)
  {
    glm::vec3 t = obj.pos.resolve(obj.propOverrides);
    glm::quat r = obj.rot.resolve(obj.propOverrides);
    glm::vec3 s = obj.scale.resolve(obj.propOverrides);
    if (glm::abs(s.x) < 1e-4f) s.x = 1e-4f;
    if (glm::abs(s.y) < 1e-4f) s.y = 1e-4f;
    if (glm::abs(s.z) < 1e-4f) s.z = 1e-4f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t);
    m = m * glm::toMat4(r);
    m = glm::scale(m, s);
    return m;
  }

  // Shared sampler (declared in components.h). Walks the same dense
  // Catmull-Rom polyline used for rendering, accumulates arc length, and
  // returns the world frame at `dist`. The basis matches the in-viewport
  // "Preview t" ghost: forward from the local tangent, up reconstructed
  // against world-up so a follower camera stays level through turns.
  bool sampleAtDistance(Object &obj, Entry &entry, float dist,
                        int branchGroup, SampleFrame &out)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    int samplesPerSeg = data.lutPerSegment.value;
    if (samplesPerSeg < 4)  samplesPerSeg = 4;
    if (samplesPerSeg > 32) samplesPerSeg = 32;

    glm::mat4 mWorld = objWorldMat(obj);

    std::vector<glm::vec3> pts;
    std::vector<float>     arcs;
    walkSpline(data, mWorld, samplesPerSeg, branchGroup,
               [&](const glm::vec3 &w, float arc) {
                 pts.push_back(w);
                 arcs.push_back(arc);
               });
    if (pts.size() < 2) return false;

    float total = arcs.back();
    out.totalLength = total;
    float d = glm::clamp(dist, 0.0f, total);

    // Locate the segment [i, i+1] containing arc length d.
    size_t i = 0;
    while (i + 1 < arcs.size() && arcs[i + 1] < d) ++i;
    size_t j = (i + 1 < pts.size()) ? i + 1 : i;

    float segLen = arcs[j] - arcs[i];
    float f = (segLen > 1e-5f) ? (d - arcs[i]) / segLen : 0.0f;

    out.pos = glm::mix(pts[i], pts[j], f);

    glm::vec3 fwd = pts[j] - pts[i];
    if (glm::length(fwd) < 1e-5f && j + 1 < pts.size()) fwd = pts[j + 1] - pts[j];
    if (glm::length(fwd) < 1e-5f) fwd = glm::vec3{0.0f, 0.0f, 1.0f};
    fwd = glm::normalize(fwd);

    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    glm::vec3 right = glm::cross(worldUp, fwd);
    if (glm::length(right) < 1e-5f) right = glm::vec3{1.0f, 0.0f, 0.0f};
    right = glm::normalize(right);

    out.fwd = fwd;
    out.up  = glm::normalize(glm::cross(fwd, right));
    return true;
  }

  void draw3D(Object &obj, Entry &entry, Editor::Viewport3D &vp,
              SDL_GPUCommandBuffer* /*cmdBuff*/, SDL_GPURenderPass* /*pass*/)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    bool isSelected = Editor::activeViewportSelection().isSelected(obj.uuid);
    glm::u8vec4 selHandleCol = Utils::Colors::kSelectionTint;
    glm::u8vec4 handleCol = glm::u8vec4{0xC0, 0xC0, 0xFF, 0xFF};

    // Per-group colors. Trunk is the primary highlight; alternates fan out.
    static const glm::u8vec4 kGroupActiveCols[8] = {
      {0xFF, 0xC8, 0x40, 0xFF},  // 0 trunk gold
      {0x40, 0xC0, 0xFF, 0xFF},  // 1 cyan
      {0xC0, 0x40, 0xFF, 0xFF},  // 2 violet
      {0x40, 0xFF, 0x80, 0xFF},  // 3 mint
      {0xFF, 0x60, 0x60, 0xFF},  // 4 red
      {0xFF, 0x90, 0x40, 0xFF},  // 5 orange
      {0x80, 0xFF, 0xC0, 0xFF},  // 6 aqua
      {0xC0, 0xC0, 0xFF, 0xFF},  // 7 lavender
    };
    static const glm::u8vec4 kGroupDimCols[8] = {
      {0x60, 0x80, 0x60, 0xFF}, {0x40, 0x60, 0x80, 0xFF},
      {0x60, 0x40, 0x80, 0xFF}, {0x40, 0x80, 0x60, 0xFF},
      {0x80, 0x40, 0x40, 0xFF}, {0x80, 0x60, 0x40, 0xFF},
      {0x40, 0x80, 0x70, 0xFF}, {0x80, 0x80, 0xC0, 0xFF},
    };

    glm::mat4 mWorld = objWorldMat(obj);
    auto groups = usedGroups(data);

    // Anchor sprite at the Object root so it's still pickable even with no points.
    Utils::Mesh::addSprite(*vp.getSprites(), obj.pos.resolve(obj.propOverrides),
                           obj.uuid, 4,
                           groups.empty() ? handleCol : kGroupActiveCols[0]);

    if (data.points.size() < 2 || groups.empty()) return;

    auto lines = vp.getLines().get();
    if (!lines) return;

    int samplesPerSeg = data.lutPerSegment.value;
    if (samplesPerSeg < 4) samplesPerSeg = 4;
    if (samplesPerSeg > 32) samplesPerSeg = 32;

    int previewBranch = data.previewGroup.value;
    if (previewBranch < 0 || previewBranch >= 8) previewBranch = 0;

    // Polyline per group. The previewed group draws bright; others dim.
    for (int g : groups) {
      bool isPreview = (g == previewBranch);
      const auto &col = isSelected
                          ? (isPreview ? kGroupActiveCols[g] : kGroupDimCols[g])
                          : kGroupDimCols[g];

      glm::vec3 prevWorld{};
      bool havePrev = false;
      walkSpline(data, mWorld, samplesPerSeg, g, [&](const glm::vec3 &w, float /*arc*/) {
        if (havePrev) Utils::Mesh::addLine(*lines, prevWorld, w, col);
        prevWorld = w;
        havePrev = true;
      });
    }

    // Sphere handles for each control point. Color matches its group.
    int selPt = -1;
    auto it = g_selectedPointByEntry.find(entry.uuid);
    if (it != g_selectedPointByEntry.end()) selPt = it->second;

    for (int i = 0; i < (int)data.points.size(); ++i) {
      const auto &p = data.points[i];
      glm::vec3 world = glm::vec3(mWorld * glm::vec4(p.pos, 1.0f));
      glm::u8vec4 col;
      if (i == selPt) col = selHandleCol;
      else if (p.branchId == previewBranch) col = kGroupActiveCols[p.branchId];
      else col = kGroupDimCols[p.branchId & 7];
      float r = (i == selPt) ? 6.0f : 4.0f;
      Utils::Mesh::addLineSphere(*lines, world, glm::vec3{r}, col);
    }

    // Scrubber ghost: small RGB axes triad at the previewed arc length on
    // the previewed group.
    if (data.tPreview.value > 0.0f && data.tPreview.value <= 1e6f) {
      float target = data.tPreview.value;
      glm::vec3 hitWorld{};
      glm::vec3 hitFwd{0,0,1};
      bool found = false;
      glm::vec3 prevW{}; bool havePW = false;
      float prevArc = 0.0f;
      walkSpline(data, mWorld, samplesPerSeg, previewBranch, [&](const glm::vec3 &w, float arc) {
        if (found) return;
        if (havePW && arc >= target) {
          float span = arc - prevArc;
          float u = span > 1e-6f ? (target - prevArc) / span : 0.0f;
          hitWorld = prevW + (w - prevW) * u;
          glm::vec3 dir = w - prevW;
          if (glm::length(dir) > 1e-6f) hitFwd = glm::normalize(dir);
          found = true;
        }
        prevW = w;
        prevArc = arc;
        havePW = true;
      });
      if (found) {
        glm::vec3 worldUp{0, 1, 0};
        if (glm::abs(glm::dot(worldUp, hitFwd)) > 0.95f) worldUp = glm::vec3{1, 0, 0};
        glm::vec3 right = glm::normalize(glm::cross(worldUp, hitFwd));
        glm::vec3 up    = glm::normalize(glm::cross(hitFwd, right));
        const float L = 12.0f;
        Utils::Mesh::addLine(*lines, hitWorld, hitWorld + hitFwd * L, glm::u8vec4{0x40, 0x40, 0xFF, 0xFF});
        Utils::Mesh::addLine(*lines, hitWorld, hitWorld + up    * L, glm::u8vec4{0x40, 0xFF, 0x40, 0xFF});
        Utils::Mesh::addLine(*lines, hitWorld, hitWorld + right * L, glm::u8vec4{0xFF, 0x40, 0x40, 0xFF});
      }
    }
  }

  // Click-to-sub-select control points + ImGuizmo manipulator for the
  // currently sub-selected point. Runs as a screen-space overlay so we can
  // mix ImGui buttons over projected world positions and call ImGuizmo
  // alongside the main Object gizmo.
  void drawOverlay(Object &obj, Entry &entry, Editor::Viewport3D & /*vp*/,
                   ImDrawList *drawList,
                   const glm::mat4 &cameraMat, const glm::mat4 &projMat,
                   const glm::vec4 &viewportRect)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (!Editor::activeViewportSelection().isSelected(obj.uuid)) return;
    if (data.points.empty()) return;

    glm::mat4 mWorld = objWorldMat(obj);
    glm::vec4 vp_glm{0.0f, 0.0f, viewportRect.z, viewportRect.w};

    int &selPt = selectedPointFor(entry.uuid);
    if (selPt >= (int)data.points.size()) selPt = -1;

    // Click-to-pick markers.
    for (int i = 0; i < (int)data.points.size(); ++i) {
      glm::vec3 world = glm::vec3(mWorld * glm::vec4(data.points[i].pos, 1.0f));
      glm::vec3 proj = glm::project(world, cameraMat, projMat, vp_glm);
      if (proj.z < 0.0f || proj.z > 1.0f) continue;

      float sx = viewportRect.x + proj.x;
      float sy = viewportRect.y + (viewportRect.w - proj.y);

      bool isSel = (i == selPt);
      float r = isSel ? 6.0f : 4.5f;
      ImU32 fill = isSel ? IM_COL32(255, 176, 46, 255) : IM_COL32(192, 192, 255, 220);
      ImU32 ring = IM_COL32(20, 20, 20, 220);
      drawList->AddCircleFilled({sx, sy}, r, fill);
      drawList->AddCircle({sx, sy}, r + 0.5f, ring, 0, 1.5f);

      // Invisible button for click pickup. Sized a bit larger than the dot
      // so off-by-pixel clicks still register.
      ImGui::SetCursorScreenPos(ImVec2(sx - 7.0f, sy - 7.0f));
      ImGui::PushID((int)(entry.uuid & 0x7FFFFFFF) ^ (i * 1013));
      if (ImGui::InvisibleButton("##path_pt", ImVec2(14, 14))) {
        selPt = i;
      }
      ImGui::PopID();
    }

    // Sub-selected point gizmo. Manipulates world translation; result is
    // converted back to local via inverse(objMat) so designers can place
    // points naturally even when the owning Object has rotation.
    if (selPt >= 0 && selPt < (int)data.points.size()) {
      glm::vec3 worldPt = glm::vec3(mWorld * glm::vec4(data.points[selPt].pos, 1.0f));
      glm::mat4 gizMat = glm::translate(glm::mat4(1.0f), worldPt);

      ImGuizmo::SetDrawlist(drawList);
      ImGuizmo::SetRect(viewportRect.x, viewportRect.y, viewportRect.z, viewportRect.w);
      ImGuizmo::PushID((int)(entry.uuid & 0x7FFFFFFF) ^ (selPt * 31337));

      bool used = ImGuizmo::Manipulate(
        glm::value_ptr(const_cast<glm::mat4&>(cameraMat)),
        glm::value_ptr(const_cast<glm::mat4&>(projMat)),
        ImGuizmo::OPERATION::TRANSLATE,
        ImGuizmo::MODE::WORLD,
        glm::value_ptr(gizMat));

      ImGuizmo::PopID();

      if (used) {
        glm::vec3 newWorld = glm::vec3(gizMat[3]);
        glm::vec4 newLocal = glm::inverse(mWorld) * glm::vec4(newWorld, 1.0f);
        data.points[selPt].pos = glm::vec3(newLocal);
        Editor::UndoRedo::getHistory().markChanged("Path: gizmo move point");
      }
    }
  }
}
