/**
* Editor-side SpriteBillboard component (added by SPBF64 fork).
* Pairs with engine: B:\Pyrite\n64\engine\src\scene\components\spriteBillboard.cpp
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../../utils/logger.h"
#include "../../../utils/colors.h"
#include "../../assetManager.h"
#include "../../../editor/pages/parts/viewport3D.h"
#include "../../../utils/meshGen.h"

#include "imgui.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/ext/matrix_projection.hpp"

namespace Project::Component::SpriteBillboard
{
  struct Data
  {
    PROP_U64(spriteUUID);
    PROP_S32(cellW);
    PROP_S32(cellH);
    PROP_S32(frame);
    PROP_S32(pivotX);
    PROP_S32(pivotY);
    PROP_BOOL(flipX);
    PROP_S32(layerIdx2D);
    PROP_FLOAT(pixelScale);     // 0 = auto from camera distance
    PROP_S32(alphaThreshold);
  };

  std::shared_ptr<void> init(Object &) {
    auto data = std::make_shared<Data>();
    data->cellW.value = 0;
    data->cellH.value = 0;
    data->frame.value = 0;
    data->pivotX.value = 0;
    data->pivotY.value = 0;
    data->layerIdx2D.value = 0;
    data->pixelScale.value = 1.0f;
    data->alphaThreshold.value = 100;
    return data;
  }

  nlohmann::json serialize(const Entry &entry) {
    Data &data = *static_cast<Data*>(entry.data.get());
    Utils::JSON::Builder b{};
    b.set(data.spriteUUID);
    b.set(data.cellW);
    b.set(data.cellH);
    b.set(data.frame);
    b.set(data.pivotX);
    b.set(data.pivotY);
    b.set(data.flipX);
    b.set(data.layerIdx2D);
    b.set(data.pixelScale);
    b.set(data.alphaThreshold);
    return b.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc) {
    auto data = std::make_shared<Data>();
    Utils::JSON::readProp(doc, data->spriteUUID);
    Utils::JSON::readProp(doc, data->cellW, 0);
    Utils::JSON::readProp(doc, data->cellH, 0);
    Utils::JSON::readProp(doc, data->frame, 0);
    Utils::JSON::readProp(doc, data->pivotX, 0);
    Utils::JSON::readProp(doc, data->pivotY, 0);
    Utils::JSON::readProp(doc, data->flipX);
    Utils::JSON::readProp(doc, data->layerIdx2D, 0);
    Utils::JSON::readProp(doc, data->pixelScale, 1.0f);
    Utils::JSON::readProp(doc, data->alphaThreshold, 100);
    return data;
  }

  void build(Object &obj, Entry &entry, Build::SceneCtx &ctx)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    uint16_t spriteIdx = 0xFFFF;
    if (data.spriteUUID.value != 0) {
      auto res = ctx.assetUUIDToIdx.find(data.spriteUUID.value);
      if (res != ctx.assetUUIDToIdx.end()) spriteIdx = res->second;
      else Utils::Logger::log(
        "SpriteBillboard: sprite UUID not found: " + std::to_string(data.spriteUUID.value),
        Utils::Logger::LEVEL_WARN
      );
    }

    // Layout MUST match engine's InitData in spriteBillboard.cpp
    ctx.fileObj.write<uint16_t>(spriteIdx);
    ctx.fileObj.write<uint16_t>((uint16_t)data.cellW.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.cellH.resolve(obj.propOverrides));
    ctx.fileObj.write<uint16_t>((uint16_t)data.frame.resolve(obj.propOverrides));
    ctx.fileObj.write<int16_t>((int16_t)data.pivotX.resolve(obj.propOverrides));
    ctx.fileObj.write<int16_t>((int16_t)data.pivotY.resolve(obj.propOverrides));
    ctx.fileObj.write<uint8_t>(data.flipX.resolve(obj.propOverrides) ? 1 : 0);
    ctx.fileObj.write<uint8_t>((uint8_t)data.layerIdx2D.resolve(obj.propOverrides));

    // Pack pixelScale to q4.4 (16 = 1.0). 0 means "auto" mode.
    float ps = data.pixelScale.resolve(obj.propOverrides);
    uint8_t psQ = 0;
    if (ps > 0.0f) {
      int q = (int)(ps * 16.0f + 0.5f);
      if (q < 1) q = 1;
      if (q > 255) q = 255;
      psQ = (uint8_t)q;
    }
    ctx.fileObj.write<uint8_t>(psQ);

    int alpha = data.alphaThreshold.resolve(obj.propOverrides);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    ctx.fileObj.write<uint8_t>((uint8_t)alpha);
  }

  void draw(Object &obj, Entry &entry)
  {
    Data &data = *static_cast<Data*>(entry.data.get());

    if (ImTable::start("Comp", &obj)) {
      ImTable::add("Name", entry.name);

      auto imageList = ctx.project->getAssets().getTypeEntries(FileType::IMAGE);
      ImTable::addAssetVecComboBox("Sprite", imageList, data.spriteUUID.resolve(obj), [](auto){});

      ImTable::addObjProp("Cell W (0=auto)", data.cellW);
      ImTable::addObjProp("Cell H (0=auto)", data.cellH);
      ImTable::addObjProp("Frame", data.frame);
      ImTable::addObjProp("Pivot X (px)", data.pivotX);
      ImTable::addObjProp("Pivot Y (px)", data.pivotY);
      ImTable::addObjProp("Flip X", data.flipX);
      ImTable::addObjProp("2D Layer Idx", data.layerIdx2D);
      ImTable::addObjProp("Pixel Scale (0=auto)", data.pixelScale);
      ImTable::addObjProp("Alpha Threshold", data.alphaThreshold);

      ImTable::end();
    }
  }

  void draw3D(Object &obj, Entry &entry, Editor::Viewport3D &vp,
              SDL_GPUCommandBuffer* /*cmdBuff*/, SDL_GPURenderPass* /*pass*/)
  {
    glm::u8vec4 col{0xCC, 0xCC, 0xFF, 0xFF};
    if (ctx.isObjectSelected(obj.uuid)) col = Utils::Colors::kSelectionTint;
    Utils::Mesh::addSprite(*vp.getSprites(), obj.pos.resolve(obj.propOverrides), obj.uuid, 4, col);
  }

  // ImGui screen-space overlay drawn after the 3D framebuffer is composited.
  // Renders the actual sprite as a textured billboard at the projected world
  // position so scene authoring matches what the runtime will display.
  void drawOverlay(Object &obj, Entry &entry, Editor::Viewport3D & /*vp*/,
                   ImDrawList *drawList,
                   const glm::mat4 &cameraMat, const glm::mat4 &projMat,
                   const glm::vec4 &viewportRect)
  {
    Data &data = *static_cast<Data*>(entry.data.get());
    if (data.spriteUUID.value == 0) return;

    auto *assetEntry = ctx.project->getAssets().getEntryByUUID(data.spriteUUID.value);
    if (!assetEntry || assetEntry->type != FileType::IMAGE) return;
    if (!assetEntry->texture) return;

    SDL_GPUTexture *gpuTex = assetEntry->texture->getGPUTex();
    if (!gpuTex) return;

    const float texW = (float)assetEntry->texture->getWidth();
    const float texH = (float)assetEntry->texture->getHeight();
    if (texW <= 0.0f || texH <= 0.0f) return;

    // Cell size: 0 means use the whole sprite
    const float cellW = data.cellW.resolve(obj.propOverrides) > 0
                          ? (float)data.cellW.resolve(obj.propOverrides) : texW;
    const float cellH = data.cellH.resolve(obj.propOverrides) > 0
                          ? (float)data.cellH.resolve(obj.propOverrides) : texH;
    const int sheetCols = (int)(texW / cellW);
    const int frame = data.frame.resolve(obj.propOverrides);
    const int cellX = sheetCols > 0 ? (frame % sheetCols) : 0;
    const int cellY = sheetCols > 0 ? (frame / sheetCols) : 0;

    // Project world position to viewport screen space (mirrors the selection
    // picking path in viewport3D.cpp).
    glm::vec3 worldPos = obj.pos.resolve(obj.propOverrides);
    glm::vec4 viewport_glm{0.0f, 0.0f, viewportRect.z, viewportRect.w};
    glm::vec3 proj = glm::project(worldPos, cameraMat, projMat, viewport_glm);
    if (proj.z < 0.0f || proj.z > 1.0f) return; // off-screen / behind camera

    // glm::project puts origin at bottom-left; ImGui at top-left.
    const float screenX = viewportRect.x + proj.x;
    const float screenY = viewportRect.y + (viewportRect.w - proj.y);

    const float pixelScale = data.pixelScale.resolve(obj.propOverrides) > 0.0f
                               ? data.pixelScale.resolve(obj.propOverrides) : 1.0f;
    const float drawW = cellW * pixelScale;
    const float drawH = cellH * pixelScale;

    // Pivot is the pixel offset within the cell that should land on the
    // projected world point. Default (cellW/2, cellH) = horizontal-center / feet.
    const float pivotX = (float)data.pivotX.resolve(obj.propOverrides);
    const float pivotY = (float)data.pivotY.resolve(obj.propOverrides);
    const float drawTLx = screenX - pivotX * pixelScale;
    const float drawTLy = screenY - pivotY * pixelScale;

    // UV sub-rect for the selected cell within the sheet
    const ImVec2 uv0{(cellX * cellW) / texW, (cellY * cellH) / texH};
    const ImVec2 uv1{((cellX + 1) * cellW) / texW, ((cellY + 1) * cellH) / texH};

    ImVec2 imgTL{drawTLx, drawTLy};
    ImVec2 imgBR{drawTLx + drawW, drawTLy + drawH};

    if (data.flipX.resolve(obj.propOverrides)) {
      drawList->AddImage(ImTextureID(gpuTex), imgTL, imgBR,
                         {uv1.x, uv0.y}, {uv0.x, uv1.y});
    } else {
      drawList->AddImage(ImTextureID(gpuTex), imgTL, imgBR, uv0, uv1);
    }

    // Pivot marker for the selected sprite
    if (ctx.isObjectSelected(obj.uuid)) {
      drawList->AddCircleFilled({screenX, screenY}, 3.0f, IM_COL32(255, 80, 80, 220));
      drawList->AddCircle({screenX, screenY}, 4.0f, IM_COL32(255, 255, 255, 220), 0, 1.5f);
    }
  }
}
