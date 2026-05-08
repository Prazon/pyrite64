/**
* Editor-side Primitive component (added by SPBF64 fork).
* Pairs with engine: n64/engine/src/scene/components/primitive.cpp
*
* Lets authors place arbitrary 3D shapes (Box, Sphere, Cylinder, Capsule,
* Cone, Pyramid, Plane) directly in the scene without authoring a glb.
* Editor preview renders as a wireframe in the chosen color (matching the
* CollBody gizmo style); the N64 runtime renders a lit solid mesh generated
* procedurally at component init.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/colors.h"
#include "../../../utils/meshGen.h"
#include "../../../editor/pages/parts/viewport3D.h"

#include "imgui.h"
#include "../../../renderer/mesh.h"
#include "glm/gtc/quaternion.hpp"

#include <algorithm>
#include <cmath>

namespace
{
  // Keep these in lock-step with engine's P64::Comp::Primitive::ShapeType.
  constexpr int32_t TYPE_BOX      = 0;
  constexpr int32_t TYPE_SPHERE   = 1;
  constexpr int32_t TYPE_CYLINDER = 2;
  constexpr int32_t TYPE_CAPSULE  = 3;
  constexpr int32_t TYPE_CONE     = 4;
  constexpr int32_t TYPE_PYRAMID  = 5;
  constexpr int32_t TYPE_PLANE    = 6;

  // Lighting baked into vertex colors so the editor preview roughly matches
  // the runtime t3d "shaded" combiner output. Values picked to give visible
  // top/side/bottom contrast under the default scene light setup.
  const glm::vec3 LIGHT_DIR = glm::normalize(glm::vec3{0.3f, 1.0f, 0.25f});
  constexpr float LIGHT_AMBIENT = 0.4f;

  glm::u8vec4 shadeColor(const glm::u8vec4 &base, const glm::vec3 &normal)
  {
    float lit = LIGHT_AMBIENT + (1.0f - LIGHT_AMBIENT)
              * std::max(0.0f, glm::dot(normal, LIGHT_DIR));
    return {
      (uint8_t)std::min(255, (int)(base.r * lit)),
      (uint8_t)std::min(255, (int)(base.g * lit)),
      (uint8_t)std::min(255, (int)(base.b * lit)),
      base.a
    };
  }

  uint16_t pushVert(Renderer::Mesh &mesh, const glm::vec3 &pos, uint32_t objId,
                    const glm::u8vec4 &color)
  {
    mesh.vertLines.push_back({pos, objId, color});
    return (uint16_t)(mesh.vertLines.size() - 1);
  }

  void pushTri(Renderer::Mesh &mesh, uint16_t a, uint16_t b, uint16_t c)
  {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
  }

  // ---- per-shape solid generators (world-space, pre-shaded) ----

  void addSolidBox(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExt,
                   const glm::quat &rot, const glm::u8vec4 &base, uint32_t objId)
  {
    auto toWorld = [&](const glm::vec3 &p) { return pos + (rot * p); };
    struct Face { glm::vec3 n; glm::vec3 v[4]; };
    const Face faces[6] = {
      {{0,1,0},   {{-halfExt.x, halfExt.y,-halfExt.z},{ halfExt.x, halfExt.y,-halfExt.z},
                   { halfExt.x, halfExt.y, halfExt.z},{-halfExt.x, halfExt.y, halfExt.z}}},
      {{0,-1,0},  {{-halfExt.x,-halfExt.y, halfExt.z},{ halfExt.x,-halfExt.y, halfExt.z},
                   { halfExt.x,-halfExt.y,-halfExt.z},{-halfExt.x,-halfExt.y,-halfExt.z}}},
      {{1,0,0},   {{ halfExt.x,-halfExt.y,-halfExt.z},{ halfExt.x, halfExt.y,-halfExt.z},
                   { halfExt.x, halfExt.y, halfExt.z},{ halfExt.x,-halfExt.y, halfExt.z}}},
      {{-1,0,0},  {{-halfExt.x,-halfExt.y, halfExt.z},{-halfExt.x, halfExt.y, halfExt.z},
                   {-halfExt.x, halfExt.y,-halfExt.z},{-halfExt.x,-halfExt.y,-halfExt.z}}},
      {{0,0,1},   {{-halfExt.x,-halfExt.y, halfExt.z},{ halfExt.x,-halfExt.y, halfExt.z},
                   { halfExt.x, halfExt.y, halfExt.z},{-halfExt.x, halfExt.y, halfExt.z}}},
      {{0,0,-1},  {{-halfExt.x, halfExt.y,-halfExt.z},{ halfExt.x, halfExt.y,-halfExt.z},
                   { halfExt.x,-halfExt.y,-halfExt.z},{-halfExt.x,-halfExt.y,-halfExt.z}}},
    };
    for (auto &f : faces) {
      glm::vec3 worldN = glm::normalize(rot * f.n);
      glm::u8vec4 c = shadeColor(base, worldN);
      uint16_t i0 = pushVert(mesh, toWorld(f.v[0]), objId, c);
      uint16_t i1 = pushVert(mesh, toWorld(f.v[1]), objId, c);
      uint16_t i2 = pushVert(mesh, toWorld(f.v[2]), objId, c);
      uint16_t i3 = pushVert(mesh, toWorld(f.v[3]), objId, c);
      pushTri(mesh, i0, i1, i2);
      pushTri(mesh, i0, i2, i3);
    }
  }

  void addSolidPlane(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExt,
                     const glm::quat &rot, const glm::u8vec4 &base, uint32_t objId)
  {
    auto toWorld = [&](const glm::vec3 &p) { return pos + (rot * p); };
    glm::vec3 worldN = glm::normalize(rot * glm::vec3{0,1,0});
    glm::u8vec4 c = shadeColor(base, worldN);
    uint16_t i0 = pushVert(mesh, toWorld({-halfExt.x, 0,-halfExt.z}), objId, c);
    uint16_t i1 = pushVert(mesh, toWorld({ halfExt.x, 0,-halfExt.z}), objId, c);
    uint16_t i2 = pushVert(mesh, toWorld({ halfExt.x, 0, halfExt.z}), objId, c);
    uint16_t i3 = pushVert(mesh, toWorld({-halfExt.x, 0, halfExt.z}), objId, c);
    pushTri(mesh, i0, i1, i2);
    pushTri(mesh, i0, i2, i3);
  }

  void addSolidPyramid(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExt,
                       const glm::quat &rot, const glm::u8vec4 &base, uint32_t objId)
  {
    auto toWorld = [&](const glm::vec3 &p) { return pos + (rot * p); };
    glm::vec3 tip{0, halfExt.y, 0};
    glm::vec3 corners[4] = {
      {-halfExt.x,-halfExt.y, halfExt.z},
      { halfExt.x,-halfExt.y, halfExt.z},
      { halfExt.x,-halfExt.y,-halfExt.z},
      {-halfExt.x,-halfExt.y,-halfExt.z},
    };
    // 4 side tris (each with own normal)
    for (int i = 0; i < 4; ++i) {
      glm::vec3 a = corners[i];
      glm::vec3 b = corners[(i+1)&3];
      glm::vec3 e1 = b - a, e2 = tip - a;
      glm::vec3 n = glm::normalize(glm::cross(e1, e2));
      glm::vec3 worldN = glm::normalize(rot * n);
      glm::u8vec4 c = shadeColor(base, worldN);
      uint16_t i0 = pushVert(mesh, toWorld(tip), objId, c);
      uint16_t i1 = pushVert(mesh, toWorld(a),   objId, c);
      uint16_t i2 = pushVert(mesh, toWorld(b),   objId, c);
      pushTri(mesh, i0, i1, i2);
    }
    // base (down-facing)
    glm::vec3 worldDown = glm::normalize(rot * glm::vec3{0,-1,0});
    glm::u8vec4 cb = shadeColor(base, worldDown);
    uint16_t b0 = pushVert(mesh, toWorld(corners[0]), objId, cb);
    uint16_t b1 = pushVert(mesh, toWorld(corners[1]), objId, cb);
    uint16_t b2 = pushVert(mesh, toWorld(corners[2]), objId, cb);
    uint16_t b3 = pushVert(mesh, toWorld(corners[3]), objId, cb);
    pushTri(mesh, b0, b2, b1);
    pushTri(mesh, b0, b3, b2);
  }

  void addSolidSphere(Renderer::Mesh &mesh, const glm::vec3 &pos, float radius,
                      const glm::quat &rot, const glm::u8vec4 &base, uint32_t objId)
  {
    constexpr int LON = 12, LAT = 8;
    auto vertOnSphere = [&](float phi, float theta) {
      float ringR = radius * sinf(phi);
      glm::vec3 p{ringR * cosf(theta), radius * cosf(phi), ringR * sinf(theta)};
      glm::vec3 n{sinf(phi)*cosf(theta), cosf(phi), sinf(phi)*sinf(theta)};
      glm::vec3 worldN = glm::normalize(rot * n);
      glm::u8vec4 c = shadeColor(base, worldN);
      return pushVert(mesh, pos + (rot * p), objId, c);
    };
    // Pole verts
    uint16_t topPole = vertOnSphere(0.0f, 0.0f);
    uint16_t botPole = vertOnSphere((float)M_PI, 0.0f);
    // Latitude rings
    std::vector<uint16_t> rings; rings.reserve(LAT * LON);
    for (int r = 1; r <= LAT; ++r) {
      float phi = (float)M_PI * (float)r / (float)(LAT + 1);
      for (int i = 0; i < LON; ++i) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)LON;
        rings.push_back(vertOnSphere(phi, theta));
      }
    }
    // Top cap
    for (int i = 0; i < LON; ++i) {
      int next = (i + 1) % LON;
      pushTri(mesh, topPole, rings[i], rings[next]);
    }
    // Middle bands
    for (int r = 0; r < LAT - 1; ++r) {
      int rowA = r * LON, rowB = rowA + LON;
      for (int i = 0; i < LON; ++i) {
        int next = (i + 1) % LON;
        pushTri(mesh, rings[rowA+i], rings[rowB+i], rings[rowB+next]);
        pushTri(mesh, rings[rowA+i], rings[rowB+next], rings[rowA+next]);
      }
    }
    // Bottom cap
    int lastRow = (LAT - 1) * LON;
    for (int i = 0; i < LON; ++i) {
      int next = (i + 1) % LON;
      pushTri(mesh, botPole, rings[lastRow+next], rings[lastRow+i]);
    }
  }

  void addSolidCylinder(Renderer::Mesh &mesh, const glm::vec3 &pos, float radius, float halfH,
                        const glm::quat &rot, const glm::u8vec4 &base, uint32_t objId)
  {
    constexpr int SEGS = 16;
    auto toWorld = [&](const glm::vec3 &p) { return pos + (rot * p); };
    std::vector<uint16_t> top(SEGS), bot(SEGS), capTop(SEGS), capBot(SEGS);
    for (int i = 0; i < SEGS; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)SEGS;
      float c = cosf(a), s = sinf(a);
      glm::vec3 sideN{c, 0, s};
      glm::vec3 sideWN = glm::normalize(rot * sideN);
      glm::u8vec4 sideCol = shadeColor(base, sideWN);
      top[i] = pushVert(mesh, toWorld({radius*c,  halfH, radius*s}), objId, sideCol);
      bot[i] = pushVert(mesh, toWorld({radius*c, -halfH, radius*s}), objId, sideCol);
    }
    glm::vec3 capUpWN = glm::normalize(rot * glm::vec3{0,1,0});
    glm::vec3 capDnWN = glm::normalize(rot * glm::vec3{0,-1,0});
    glm::u8vec4 capUpCol = shadeColor(base, capUpWN);
    glm::u8vec4 capDnCol = shadeColor(base, capDnWN);
    uint16_t topCenter = pushVert(mesh, toWorld({0, halfH, 0}), objId, capUpCol);
    uint16_t botCenter = pushVert(mesh, toWorld({0,-halfH, 0}), objId, capDnCol);
    for (int i = 0; i < SEGS; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)SEGS;
      capTop[i] = pushVert(mesh, toWorld({radius*cosf(a),  halfH, radius*sinf(a)}), objId, capUpCol);
      capBot[i] = pushVert(mesh, toWorld({radius*cosf(a), -halfH, radius*sinf(a)}), objId, capDnCol);
    }
    // Side
    for (int i = 0; i < SEGS; ++i) {
      int n = (i + 1) % SEGS;
      pushTri(mesh, top[i], bot[i], bot[n]);
      pushTri(mesh, top[i], bot[n], top[n]);
    }
    // Caps
    for (int i = 0; i < SEGS; ++i) {
      int n = (i + 1) % SEGS;
      pushTri(mesh, topCenter, capTop[n], capTop[i]);
      pushTri(mesh, botCenter, capBot[i], capBot[n]);
    }
  }

  void addSolidCone(Renderer::Mesh &mesh, const glm::vec3 &pos, float radius, float halfH,
                    const glm::quat &rot, const glm::u8vec4 &base, uint32_t objId)
  {
    constexpr int SEGS = 16;
    auto toWorld = [&](const glm::vec3 &p) { return pos + (rot * p); };
    glm::vec3 capDnWN = glm::normalize(rot * glm::vec3{0,-1,0});
    glm::u8vec4 capDnCol = shadeColor(base, capDnWN);
    uint16_t baseCenter = pushVert(mesh, toWorld({0,-halfH, 0}), objId, capDnCol);
    std::vector<uint16_t> sideTip(SEGS), sideBase(SEGS), capRing(SEGS);
    float slope = halfH / std::max(radius, 0.0001f);
    for (int i = 0; i < SEGS; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)SEGS;
      float c = cosf(a), s = sinf(a);
      glm::vec3 n = glm::normalize(glm::vec3{c, slope, s});
      glm::vec3 wn = glm::normalize(rot * n);
      glm::u8vec4 col = shadeColor(base, wn);
      sideTip[i]  = pushVert(mesh, toWorld({0,            halfH, 0}),                objId, col);
      sideBase[i] = pushVert(mesh, toWorld({radius*c,    -halfH, radius*s}),          objId, col);
      capRing[i]  = pushVert(mesh, toWorld({radius*c,    -halfH, radius*s}),          objId, capDnCol);
    }
    for (int i = 0; i < SEGS; ++i) {
      int nx = (i + 1) % SEGS;
      pushTri(mesh, sideTip[i], sideBase[nx], sideBase[i]);
      pushTri(mesh, baseCenter, capRing[i], capRing[nx]);
    }
  }

  void addSolidCapsule(Renderer::Mesh &mesh, const glm::vec3 &pos, float radius, float innerHalfH,
                       const glm::quat &rot, const glm::u8vec4 &base, uint32_t objId)
  {
    constexpr int LON = 12, CAP_LAT = 4;
    auto toWorld = [&](const glm::vec3 &p) { return pos + (rot * p); };
    auto vertCap = [&](float phi, float theta, float yOffset, bool topCap) {
      glm::vec3 n{sinf(phi)*cosf(theta), (topCap ? cosf(phi) : -cosf(phi)), sinf(phi)*sinf(theta)};
      glm::vec3 p{radius * sinf(phi) * cosf(theta),
                  yOffset + (topCap ? radius * cosf(phi) : -radius * cosf(phi)),
                  radius * sinf(phi) * sinf(theta)};
      glm::vec3 wn = glm::normalize(rot * n);
      glm::u8vec4 c = shadeColor(base, wn);
      return pushVert(mesh, toWorld(p), objId, c);
    };
    // Cylinder body verts (per-vertex side normal)
    std::vector<uint16_t> cylTop(LON), cylBot(LON);
    for (int i = 0; i < LON; ++i) {
      float a = 2.0f * (float)M_PI * (float)i / (float)LON;
      float c = cosf(a), s = sinf(a);
      glm::vec3 n{c, 0, s};
      glm::vec3 wn = glm::normalize(rot * n);
      glm::u8vec4 col = shadeColor(base, wn);
      cylTop[i] = pushVert(mesh, toWorld({radius*c,  innerHalfH, radius*s}), objId, col);
      cylBot[i] = pushVert(mesh, toWorld({radius*c, -innerHalfH, radius*s}), objId, col);
    }
    // Cylinder side
    for (int i = 0; i < LON; ++i) {
      int n = (i + 1) % LON;
      pushTri(mesh, cylTop[i], cylBot[i], cylBot[n]);
      pushTri(mesh, cylTop[i], cylBot[n], cylTop[n]);
    }
    // Caps (each: rings + pole)
    for (int cap = 0; cap < 2; ++cap) {
      bool topCap = (cap == 0);
      float yOff = topCap ? innerHalfH : -innerHalfH;
      std::vector<std::vector<uint16_t>> rings(CAP_LAT, std::vector<uint16_t>(LON));
      for (int r = 0; r < CAP_LAT; ++r) {
        float phi = (float)M_PI * 0.5f * (float)(r + 1) / (float)(CAP_LAT + 1);
        // r=0 closest to equator (small phi), r=CAP_LAT-1 close to pole
        for (int i = 0; i < LON; ++i) {
          float theta = 2.0f * (float)M_PI * (float)i / (float)LON;
          rings[r][i] = vertCap(phi, theta, yOff, topCap);
        }
      }
      uint16_t pole = vertCap(0.0f, 0.0f, yOff, topCap);
      // Connect equator (cyl ring) to first cap ring
      auto &equatorRing = topCap ? cylTop : cylBot;
      for (int i = 0; i < LON; ++i) {
        int n = (i + 1) % LON;
        if (topCap) {
          pushTri(mesh, equatorRing[i], rings[0][i], rings[0][n]);
          pushTri(mesh, equatorRing[i], rings[0][n], equatorRing[n]);
        } else {
          pushTri(mesh, equatorRing[i], rings[0][n], rings[0][i]);
          pushTri(mesh, equatorRing[i], equatorRing[n], rings[0][n]);
        }
      }
      // Between rings
      for (int r = 0; r < CAP_LAT - 1; ++r) {
        for (int i = 0; i < LON; ++i) {
          int n = (i + 1) % LON;
          if (topCap) {
            pushTri(mesh, rings[r][i], rings[r+1][i], rings[r+1][n]);
            pushTri(mesh, rings[r][i], rings[r+1][n], rings[r][n]);
          } else {
            pushTri(mesh, rings[r][i], rings[r+1][n], rings[r+1][i]);
            pushTri(mesh, rings[r][i], rings[r][n], rings[r+1][n]);
          }
        }
      }
      // Last ring to pole
      auto &lastRing = rings[CAP_LAT - 1];
      for (int i = 0; i < LON; ++i) {
        int n = (i + 1) % LON;
        if (topCap)  pushTri(mesh, lastRing[i], pole, lastRing[n]);
        else         pushTri(mesh, lastRing[i], lastRing[n], pole);
      }
    }
  }
}

namespace Project::Component::Primitive
{
  struct Data
  {
    PROP_S32(type);
    PROP_VEC3(halfExtend);
    PROP_VEC4(color);
    PROP_S32(layerIdx);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->type.value = TYPE_BOX;
    data->halfExtend.value = {16.0f, 16.0f, 16.0f};
    data->color.value = {0.7f, 0.7f, 0.7f, 1.0f};
    data->layerIdx.value = 0;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    return Utils::JSON::Builder{}
      .set(data.type)
      .set(data.halfExtend)
      .set(data.color)
      .set(data.layerIdx)
      .doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->type, TYPE_BOX);
    Utils::JSON::readProp(doc, data->halfExtend, glm::vec3{16.0f, 16.0f, 16.0f});
    Utils::JSON::readProp(doc, data->color, glm::vec4{0.7f, 0.7f, 0.7f, 1.0f});
    Utils::JSON::readProp(doc, data->layerIdx, 0);
    return data;
  }

  // Binary layout MUST match engine's InitData in primitive.cpp.
  void build(Object& obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    auto type = data.type.resolve(obj.propOverrides);
    auto ext  = data.halfExtend.resolve(obj.propOverrides);
    auto col  = data.color.resolve(obj.propOverrides);

    ctx.fileObj.write<uint8_t>((uint8_t)type);
    ctx.fileObj.write<uint8_t>((uint8_t)data.layerIdx.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.r * 255.0f), 0, 255));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.g * 255.0f), 0, 255));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.b * 255.0f), 0, 255));
    ctx.fileObj.write<uint8_t>((uint8_t)std::clamp((int)(col.a * 255.0f), 0, 255));
    ctx.fileObj.align(4);
    ctx.fileObj.write<float>(ext.x);
    ctx.fileObj.write<float>(ext.y);
    ctx.fileObj.write<float>(ext.z);
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      ImTable::addComboBox("Shape", data.type.value,
        {"Box", "Sphere", "Cylinder", "Capsule", "Cone", "Pyramid", "Plane"});

      auto &ext = data.halfExtend.resolve(obj.propOverrides);
      auto type = data.type.resolve(obj.propOverrides);

      switch(type) {
        case TYPE_BOX:
          ImTable::addObjProp("Half Size", data.halfExtend);
          break;
        case TYPE_SPHERE:
          ImTable::add("Radius", ext.y);
          ext.x = ext.y;
          ext.z = ext.y;
          break;
        case TYPE_CYLINDER:
        case TYPE_CONE:
          ImTable::add("Radius", ext.x);
          ImTable::add("Half Height", ext.y);
          ext.z = ext.x;
          break;
        case TYPE_CAPSULE:
          ImTable::add("Radius", ext.x);
          ImTable::add("Inner Half Height", ext.y);
          ext.z = ext.x;
          break;
        case TYPE_PYRAMID:
          ImTable::add("Base X Half", ext.x);
          ImTable::add("Half Height", ext.y);
          ImTable::add("Base Z Half", ext.z);
          break;
        case TYPE_PLANE:
          ImTable::add("Half X", ext.x);
          ImTable::add("Half Z", ext.z);
          ext.y = 0.0f;
          break;
      }

      ImTable::addObjProp("Color", data.color);

      auto scene = ctx.project->getScenes().getLoadedScene();
      if (scene) {
        std::vector<const char*> layerNames{};
        for (auto &layer : scene->conf.layers3D) {
          layerNames.push_back(layer.name.value.c_str());
        }
        ImTable::addObjProp<int32_t>("Draw-Layer", data.layerIdx,
          [&layerNames](int32_t *layer) {
            return ImGui::Combo("##", layer, layerNames.data(), (int)layerNames.size());
          }, nullptr);
      }

      ImTable::end();

      // Phase 4 polish: one-click matching CollBody. Convenient when authoring
      // platforms / blockers so visual + collider stay in sync.
      if (ImGui::Button("Add Matching CollBody")) {
        size_t before = obj.components.size();
        obj.addComponent(5 /* CollBody id */);
        if (obj.components.size() > before) {
          auto &newEntry = obj.components.back();
          auto &collDef = TABLE[5];
          nlohmann::json doc = collDef.funcSerialize(newEntry);
          doc["type"] = (int)data.type.resolve(obj.propOverrides);
          glm::vec3 he = data.halfExtend.resolve(obj.propOverrides);
          doc["halfExtend"] = nlohmann::json::array({he.x, he.y, he.z});
          newEntry.data = collDef.funcDeserialize(doc);
        }
      }
    }
  }

  void draw3D(Object& obj, Entry &entry, Editor::Viewport3D &vp,
              SDL_GPUCommandBuffer* /*cmdBuff*/, SDL_GPURenderPass* /*pass*/)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    auto &objPos = obj.pos.resolve(obj.propOverrides);
    auto &objRot = obj.rot.resolve(obj.propOverrides);
    auto &objScale = obj.scale.resolve(obj.propOverrides);

    glm::vec3 halfExt = data.halfExtend.resolve(obj.propOverrides) * objScale;
    auto col = data.color.resolve(obj.propOverrides);
    glm::u8vec4 base{
      (uint8_t)std::clamp((int)(col.r * 255.0f), 0, 255),
      (uint8_t)std::clamp((int)(col.g * 255.0f), 0, 255),
      (uint8_t)std::clamp((int)(col.b * 255.0f), 0, 255),
      (uint8_t)std::clamp((int)(col.a * 255.0f), 0, 255)
    };

    auto &solidMesh = *vp.getPrimitives();
    auto type = data.type.resolve(obj.propOverrides);

    switch(type) {
      case TYPE_BOX:
        addSolidBox(solidMesh, objPos, halfExt, objRot, base, obj.uuid);
        break;
      case TYPE_PLANE:
        addSolidPlane(solidMesh, objPos, halfExt, objRot, base, obj.uuid);
        break;
      case TYPE_SPHERE:
        addSolidSphere(solidMesh, objPos, halfExt.y, objRot, base, obj.uuid);
        break;
      case TYPE_CYLINDER:
        addSolidCylinder(solidMesh, objPos, halfExt.x, halfExt.y, objRot, base, obj.uuid);
        break;
      case TYPE_CAPSULE:
        addSolidCapsule(solidMesh, objPos, halfExt.x, halfExt.y, objRot, base, obj.uuid);
        break;
      case TYPE_CONE:
        addSolidCone(solidMesh, objPos, halfExt.x, halfExt.y, objRot, base, obj.uuid);
        break;
      case TYPE_PYRAMID:
        addSolidPyramid(solidMesh, objPos, halfExt, objRot, base, obj.uuid);
        break;
    }

    // Selection outline (drawn after solids on top via the lines pipeline).
    if (Editor::activeViewportSelection().isSelected(obj.uuid)) {
      glm::u8vec4 outline = Utils::Colors::kSelectionTint;
      switch(type) {
        case TYPE_BOX:
        case TYPE_PLANE:
          Utils::Mesh::addLineBox(*vp.getLines(), objPos, halfExt, outline, objRot); break;
        case TYPE_SPHERE:
          Utils::Mesh::addLineSphere(*vp.getLines(), objPos, halfExt, outline, objRot); break;
        case TYPE_CYLINDER:
          Utils::Mesh::addLineCylinder(*vp.getLines(), objPos, halfExt, outline, objRot); break;
        case TYPE_CAPSULE:
          Utils::Mesh::addLineCapsule(*vp.getLines(), objPos, halfExt, outline, objRot); break;
        case TYPE_CONE:
          Utils::Mesh::addLineCone(*vp.getLines(), objPos, halfExt, outline, objRot); break;
        case TYPE_PYRAMID:
          Utils::Mesh::addLinePyramid(*vp.getLines(), objPos, halfExt, outline, objRot); break;
      }
    }
  }
}
