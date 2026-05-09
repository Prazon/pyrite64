/**
 * @copyright 2026 - Prazon
 * @license MIT
 */
#include "matPreviewViewport.h"

#include <filesystem>

#include "../../../../context.h"
#include "../../../../utils/logger.h"

#include "tiny3d/tools/gltf_importer/src/parser.h"

namespace fs = std::filesystem;

bool Editor::MaterialPreviewViewport::ensureHost()
{
  if (hostLoaded) return true;
  if (hostFailed) return false;
  if (!ctx.project) return false;

  fs::path glbPath = "data/preview/material_host.glb";
  if (!fs::exists(glbPath)) {
    Utils::Logger::log("MaterialPreviewViewport: missing " + glbPath.string(),
                       Utils::Logger::LEVEL_WARN);
    hostFailed = true;
    return false;
  }

  try {
    hostModel = std::make_unique<Project::Assets::Model3D>();

    // The cube has no external textures, so most of these path fields are
    // unused. We still pass real paths so parseGLTF doesn't crash on a default
    // string accessor it might exercise. getMaterialInfo returns false to
    // accept the t3dm defaults.
    hostModel->t3dm = T3DM::parseGLTF(glbPath.string().c_str(), {
      .globalScale    = 1.0f,
      .animSampleRate = 60,
      .createBVH      = false,
      .verbose        = false,
      .assetPath      = "data/preview/",
      .assetPathFull  = fs::absolute("data/preview/").string(),
      .projectPath    = fs::current_path(),
      .getMaterialInfo = [](const std::string&, T3DM::Config::MatInfo&) -> bool {
        return false;
      },
    });

    // Seed the material map so N64Mesh::draw's per-part lookup finds an
    // entry. Each render setMaterial() will overwrite these with the
    // material currently being previewed.
    auto &assets = ctx.project->getAssets();
    for (const auto &t3dMat : hostModel->t3dm.materials) {
      auto &mat = hostModel->materials[t3dMat.first];
      mat.fromT3D(assets, t3dMat.second);
    }

    hostMesh = std::make_shared<Renderer::N64Mesh>();
    hostMesh->fromT3DM(*hostModel, assets);

    viewport.setMesh(0, hostMesh, hostModel.get());
    // Material previews want a tight, bounded view: framed close (1.6 radii)
    // so the cube actually fills the cell, and zoom locked to [1.05, 3.5]
    // radii so users can't dolly inside the cube or shrink it to a dot.
    viewport.setFraming(1.6f, 1.05f, 3.5f);
    hostLoaded = true;
    return true;
  } catch (const std::exception &e) {
    Utils::Logger::log(std::string("MaterialPreviewViewport: load failed - ") + e.what(),
                       Utils::Logger::LEVEL_ERROR);
    hostFailed = true;
    return false;
  }
}

void Editor::MaterialPreviewViewport::applyPendingIfReady()
{
  if (!hostLoaded || !hasPending || !hostModel) return;
  for (auto &[name, partMat] : hostModel->materials) {
    partMat = pendingMat;
  }
  hasPending = false;
}

void Editor::MaterialPreviewViewport::setMaterial(const Project::Assets::Material &mat)
{
  pendingMat = mat;
  hasPending = true;
  applyPendingIfReady();
}

void Editor::MaterialPreviewViewport::draw(ImVec2 size)
{
  if (!ensureHost()) {
    ImGui::Dummy(size);
    return;
  }
  applyPendingIfReady();
  viewport.draw(size);
}

SDL_GPUTexture* Editor::MaterialPreviewViewport::renderHeadless(ImVec2 size)
{
  if (!ensureHost()) return nullptr;
  applyPendingIfReady();
  viewport.renderHeadless(size);
  return viewport.getTexture();
}

SDL_GPUTexture* Editor::MaterialPreviewViewport::getTexture() const
{
  return viewport.getTexture();
}

bool Editor::MaterialPreviewViewport::readPixels(std::vector<uint8_t> &out,
                                                 uint32_t &outW, uint32_t &outH)
{
  return viewport.readPixels(out, outW, outH);
}
