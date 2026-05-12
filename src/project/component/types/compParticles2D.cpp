/**
* Editor-side Particles2D component.
* Pairs with engine: n64/engine/src/scene/components/particles2D.cpp
*
* The editor stores only the configuration (max count, gravity, palette).
* Runtime spawn() events come from gameplay code (P64_NODE helpers); the
* editor does not author per-particle bursts.
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"

#include "imgui.h"

namespace Project::Component::Particles2D
{
  struct Data
  {
    PROP_S32(maxParticles);
    PROP_FLOAT(gravityY);
    PROP_VEC4(pal0);
    PROP_VEC4(pal1);
    PROP_VEC4(pal2);
    PROP_VEC4(pal3);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->maxParticles.value = 64;
    data->gravityY.value = 0.0f;
    data->pal0.value = {1.0f, 1.0f, 1.0f, 1.0f};
    data->pal1.value = {1.0f, 0.78f, 0.0f, 1.0f};
    data->pal2.value = {0.0f, 1.0f, 0.78f, 1.0f};
    data->pal3.value = {0.78f, 0.78f, 0.78f, 1.0f};
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.maxParticles);
    b.set(data.gravityY);
    b.set(data.pal0);
    b.set(data.pal1);
    b.set(data.pal2);
    b.set(data.pal3);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->maxParticles, 64);
    Utils::JSON::readProp(doc, data->gravityY, 0.0f);
    Utils::JSON::readProp(doc, data->pal0, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f});
    Utils::JSON::readProp(doc, data->pal1, glm::vec4{1.0f, 0.78f, 0.0f, 1.0f});
    Utils::JSON::readProp(doc, data->pal2, glm::vec4{0.0f, 1.0f, 0.78f, 1.0f});
    Utils::JSON::readProp(doc, data->pal3, glm::vec4{0.78f, 0.78f, 0.78f, 1.0f});
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    auto toU8 = [](float v) {
      if (v < 0.0f) return (uint8_t)0;
      if (v > 1.0f) return (uint8_t)255;
      return (uint8_t)(v * 255.0f + 0.5f);
    };

    auto writePal = [&](const glm::vec4 &c) {
      ctx.fileObj.write<uint8_t>(toU8(c.r));
      ctx.fileObj.write<uint8_t>(toU8(c.g));
      ctx.fileObj.write<uint8_t>(toU8(c.b));
      ctx.fileObj.write<uint8_t>(toU8(c.a));
    };

    int mp = data.maxParticles.resolve(obj.propOverrides);
    if (mp < 1) mp = 1;
    if (mp > 4096) mp = 4096;

    // Layout MUST match engine InitData in particles2D.cpp (sizeof == 24).
    ctx.fileObj.write<uint16_t>((uint16_t)mp);
    ctx.fileObj.write<uint16_t>(0);
    ctx.fileObj.write<float>(data.gravityY.resolve(obj.propOverrides));
    writePal(data.pal0.resolve(obj.propOverrides));
    writePal(data.pal1.resolve(obj.propOverrides));
    writePal(data.pal2.resolve(obj.propOverrides));
    writePal(data.pal3.resolve(obj.propOverrides));
  }

  void draw2D(Object &obj, Entry &entry, ImDrawList *dl,
              ImVec2 originScreen, float zoom,
              ImVec2 *outMin, ImVec2 *outMax)
  {
    // No persistent particle state at edit time. Draw a small emitter
    // marker so the user can see + select the Object in the 2D viewport.
    float r = 6.0f * (zoom < 1.0f ? 1.0f : zoom);
    ImVec2 a{originScreen.x - r, originScreen.y - r};
    ImVec2 b{originScreen.x + r, originScreen.y + r};
    dl->AddCircleFilled(originScreen, r * 0.7f, IM_COL32(200, 220, 255, 100));
    dl->AddCircle(originScreen, r, IM_COL32(200, 220, 255, 220), 0, 1.5f);

    if (outMin) *outMin = a;
    if (outMax) *outMax = b;
    (void)entry;
    (void)obj;
  }

  void widgetSize(Object &, Entry &, int *outW, int *outH)
  {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);
      ImTable::addObjProp("Max particles", data.maxParticles);
      ImTable::addObjProp("Gravity Y (px/s^2)", data.gravityY);
      ImTable::addColor("Palette 0", data.pal0.value, true);
      ImTable::addColor("Palette 1", data.pal1.value, true);
      ImTable::addColor("Palette 2", data.pal2.value, true);
      ImTable::addColor("Palette 3", data.pal3.value, true);
      ImTable::end();
    }
  }
}
