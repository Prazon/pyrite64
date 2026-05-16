/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "../project/scene/object.h"
#include "n64Mesh.h"
#include "../context.h"
#include "../project/assetManager.h"
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include "scene.h"
#include "../shader/defines.h"
#include "n64/n64Material.h"

namespace fs = std::filesystem;
extern SDL_GPUSampler *texSamplerRepeat; // @TODO make sampler manager? is this even needed?

namespace
{
  constinit glm::vec4 lastPrim{};
  constinit glm::vec4 lastEnv{};
}

void Renderer::N64Mesh::fromT3DM(const Project::Assets::Model3D &model3d, Project::AssetManager &assetManager)
{
  loaded = false;
  mesh.vertices.clear();
  mesh.indices.clear();
  parts.clear();
  uvDiag = {};

  auto &t3dmData = model3d.t3dm;
  parts.resize(t3dmData.models.size());
  auto part = parts.begin();

  uint16_t idx = 0;
  for (auto &model : t3dmData.models)
  {
    part->indicesOffset = mesh.indices.size();
    part->indicesCount = model.triangles.size() * 3;

    part->materialName = model.materialName;
    part->texBindings[0].texture = assetManager.getFallbackTexture()->getGPUTex();
    part->texBindings[0].sampler = texSamplerRepeat;
    part->texBindings[1] = part->texBindings[0];

    // Track this part's raw s10.5 UV extents to detect models that exceed the
    // RDP's S10.5 texel range (see UvRangeDiag). int16 saturates near
    // ±1024px, and a UV span past that also wraps under wrappedMirror().
    int16_t minS = INT16_MAX, maxS = INT16_MIN;
    int16_t minT = INT16_MAX, maxT = INT16_MIN;

    //model.material.colorCombiner
    for (auto &tri : model.triangles) {

      for (auto &vert : tri.vert) {

        minS = std::min(minS, vert.s); maxS = std::max(maxS, vert.s);
        minT = std::min(minT, vert.t); maxT = std::max(maxT, vert.t);

        uint8_t r = (vert.rgba >> 24) & 0xFF;
        uint8_t g = (vert.rgba >> 16) & 0xFF;
        uint8_t b = (vert.rgba >> 8) & 0xFF;
        uint8_t a = (vert.rgba >> 0) & 0xFF;

        mesh.vertices.push_back({
          {vert.pos[0], vert.pos[1], vert.pos[2]},
          vert.norm,
          {r,g,b,a},
          glm::ivec2(vert.s, vert.t),
          {(int16_t)vert.boneIndex, 0},
        });
        /*printf("v: %d,%d,%d norm: %d uv: %d,%d col: %08X\n",
          vert.pos[0], vert.pos[1], vert.pos[2],
          vert.norm,
          vert.s, vert.t,
          vert.rgba
        );*/
      }

      mesh.indices.push_back(idx++);
      mesh.indices.push_back(idx++);
      mesh.indices.push_back(idx++);
    }

    if (maxS >= minS) // part had at least one vert
    {
      // s10.5 raw -> pixels. Flag both large absolute coords (int16
      // saturation) and large UV spans (tiled past the S10.5 range, which
      // wraps under wrappedMirror and produces the rainbow aliasing).
      int absPx  = std::max({ std::abs((int)minS), std::abs((int)maxS),
                              std::abs((int)minT), std::abs((int)maxT) }) / 32;
      int spanPx = std::max((int)maxS - minS, (int)maxT - minT) / 32;
      int worst  = std::max(absPx, spanPx);
      if (worst > S10_5_MAX_PIXEL && worst > uvDiag.worstPixel)
      {
        uvDiag.outOfRange   = true;
        uvDiag.worstPixel   = worst;
        uvDiag.materialName = part->materialName;
      }
    }

    ++part;
  }
}

void Renderer::N64Mesh::recreate(Renderer::Scene &sc) {
  scene = &sc;
  mesh.recreate(sc);
  loaded = true;
}

void Renderer::N64Mesh::draw(
  SDL_GPURenderPass* pass, SDL_GPUCommandBuffer *cmdBuff, UniformsObject &uniforms,
  const ObjectRef &ref
) {
  if (!scene)return;

  // Bits the caller may set on uniforms.mat.flags that should survive past
  // `uniforms.mat = part.material;` below. LIGHT_MODE_ADD is the existing
  // additive-light layer toggle; T3D_FLAG_NO_LIGHT lets the asset-preview
  // viewport bypass scene lighting (added by SPBF64 fork).
  uint32_t flagsGlobal = uniforms.mat.flags & (LIGHT_MODE_ADD | T3D_FLAG_NO_LIGHT);

  auto drawPart = [&](MeshPart &part)
  {
    uint32_t blender = uniforms.mat.blender.x;

    uint32_t slotIdx = 0;
    auto matEntry = ref.model->materials.find(part.materialName);
    if(matEntry != ref.model->materials.end()) {
      auto mat = matEntry->second;

      auto resolveTex = [&](Project::Assets::MaterialTex &tex, int texBinding)
      {
        if (tex.set.value) {
          // Dynamic material-instance slots only apply when an instance is
          // bound. Asset previews (matInstance==nullptr) skip these and use
          // whatever texture the model's own material declares.
          if (ref.matInstance) {
            if(tex.dynType.value == tex.DYN_TYPE_FULL && slotIdx < 8) {
              tex = ref.matInstance->texSlots[slotIdx];
              ++slotIdx;
            }
            else if(tex.dynType.value == tex.DYN_TYPE_TILE && slotIdx < 8) {
              tex.offset = ref.matInstance->texSlots[slotIdx].offset;
              ++slotIdx;
            }
          }
          auto texEntry = ctx.project->getAssets().getEntryByUUID(tex.texUUID.value);
          if (texEntry && texEntry->texture) {
            part.texBindings[texBinding].texture = texEntry->texture->getGPUTex();
          }
        }
      };

      // Resolve textures + convert the material every draw, regardless of
      // whether an instance is bound — asset previews still need their
      // textures and tile/CC settings to show correctly.
      resolveTex(mat.tex0, 0);
      resolveTex(mat.tex1, 1);
      N64Material::convert(part, mat);
    }


    if(ref.matInstance)
    {
      if(part.material.flags & UniformN64Material::FLAG_SET_PRIM_COL) {
        lastPrim = part.material.colPrim;
      } else {
        if(ref.matInstance->setPrim.resolve(ref.obj)) {
          lastPrim = ref.matInstance->prim.resolve(ref.obj);
        }
      }

      if(part.material.flags & UniformN64Material::FLAG_SET_ENV_COL) {
        lastEnv = part.material.colEnv;
      } else {
        if(ref.matInstance->setEnv.resolve(ref.obj)) {
          lastEnv = ref.matInstance->env.resolve(ref.obj);
        }
      }
    }

    uniforms.mat = part.material;
    if (ref.matInstance) {
      // Instance-driven prim/env overrides accumulated above.
      uniforms.mat.colPrim = lastPrim;
      uniforms.mat.colEnv = lastEnv;
    }
    // Without a material instance (asset preview path), keep the prim/env
    // colors that N64Material::convert wrote into part.material — overriding
    // them with the file-static lastPrim/lastEnv would bleed colors from
    // whatever model was drawn last.
    uniforms.mat.blender.x = blender;
    uniforms.mat.flags |= flagsGlobal;

    // @TODO: move out

    uint32_t MAX_LIGHTS = uniforms.mat.lightColor.size();

    const auto &lights = scene->getLights();
    int lightIdx = 0;
    for (auto &light : lights) {
      if (light.type == 0) {
        uniforms.mat.ambientColor = light.color;
      } else {
        if (lightIdx < MAX_LIGHTS)
        {
          if(light.type == 2) {// point light
            uniforms.mat.lightDir[lightIdx] = light.pos;
            uniforms.mat.lightDir[lightIdx].w = light.size;
          } else {
            uniforms.mat.lightDir[lightIdx] = glm::vec4(light.dir, 0);
          }

          uniforms.mat.lightColor[lightIdx] = light.color;
          ++lightIdx;
        }
      }
    }

    if(ref.isCollision) {
      uniforms.mat.flags |= DRAW_SHADER_COLLISION;
    } else {
      uniforms.mat.flags &= ~DRAW_SHADER_COLLISION;
    }

    SDL_BindGPUFragmentSamplers(pass, 0, part.texBindings, 2);
    SDL_PushGPUVertexUniformData(cmdBuff, 1, &uniforms, sizeof(uniforms));
    SDL_PushGPUFragmentUniformData(cmdBuff, 0, &uniforms, sizeof(uniforms));

    mesh.draw(pass, part.indicesOffset, part.indicesCount);
  };

  if(ref.partsIndices.empty())
  {
    for (auto &part : parts) {
      drawPart(part);
    }
  } else {
    for (auto idx : ref.partsIndices) {
      if (idx < parts.size()) {
        drawPart(parts[idx]);
      }
    }
  }
  //mesh.draw(pass);
}
