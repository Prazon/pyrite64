/**
 * @copyright 2026 - Prazon
 * @license MIT
 */
#include "modelThumbnailCache.h"

#include <filesystem>
#include <system_error>

#include <SDL3/SDL.h>
#include "SDL3_image/SDL_image.h"

#include "../../../../context.h"
#include "../../../../project/cacheDir.h"
#include "../../../../project/project.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"

namespace fs = std::filesystem;

namespace
{
  constexpr const char *kSubsystem = "modelThumb";

  fs::path thumbPath(uint64_t uuid)
  {
    if (!ctx.project) return {};
    return Project::Cache::fileFor(*ctx.project, kSubsystem,
                                   std::to_string(uuid) + ".png");
  }

  bool isCacheStale(const Project::AssetManagerEntry &asset)
  {
    auto pngPath = thumbPath(asset.getUUID());
    if (pngPath.empty()) return true;
    std::error_code ec;
    if (!fs::exists(pngPath, ec)) return true;

    auto pngTs = Utils::FS::getFileAge(pngPath);
    auto srcTs = Utils::FS::getFileAge(asset.path);
    return srcTs > pngTs;
  }

  bool savePngFromBytes(const fs::path &path,
                        const std::vector<uint8_t> &rgba,
                        uint32_t w, uint32_t h)
  {
    if (rgba.size() < (size_t)w * h * 4) return false;
    SDL_Surface *surf = SDL_CreateSurfaceFrom(
      (int)w, (int)h, SDL_PIXELFORMAT_RGBA32,
      (void*)rgba.data(), (int)(w * 4)
    );
    if (!surf) return false;
    bool ok = IMG_SavePNG(surf, path.string().c_str());
    SDL_DestroySurface(surf);
    return ok;
  }
}

bool Editor::ModelThumbnailCache::tryLoadFromDisk(uint64_t uuid, Entry &entry)
{
  auto pngPath = thumbPath(uuid);
  if (pngPath.empty()) return false;
  std::error_code ec;
  if (!fs::exists(pngPath, ec)) return false;
  if (!ctx.gpu) return false;

  // Staleness check defers to the asset entry; caller only invokes this on
  // first touch, so we look up the entry here rather than threading it in.
  if (ctx.project) {
    if (auto *asset = ctx.project->getAssets().getEntryByUUID(uuid)) {
      if (isCacheStale(*asset)) return false;
    }
  }

  try {
    entry.loadedTex = std::make_unique<Renderer::Texture>(
      ctx.gpu, pngPath.string(), false, 0, 0
    );
    if (!entry.loadedTex->getGPUTex()) {
      entry.loadedTex.reset();
      return false;
    }
  } catch (const std::exception &e) {
    Utils::Logger::log(
      std::string("ModelThumbnailCache: failed to load cached PNG ")
      + pngPath.string() + " - " + e.what(),
      Utils::Logger::LEVEL_WARN
    );
    entry.loadedTex.reset();
    return false;
  }

  entry.everRendered = true;
  entry.dirty = false;
  return true;
}

bool Editor::ModelThumbnailCache::renderAndPersist(
  const Project::AssetManagerEntry &asset, Entry &entry)
{
  // No geometry to render at all: bail. Caller falls back to the glyph.
  if (!asset.mesh3D) return false;

  if (!entry.viewport) {
    entry.viewport = std::make_unique<AssetPreviewViewport>();
    entry.boundMeshRaw = nullptr;
  }
  void *meshRaw = asset.mesh3D.get();
  if (entry.boundMeshRaw != meshRaw) {
    entry.viewport->setMesh(asset.getUUID(), asset.mesh3D, &asset.model);
    entry.boundMeshRaw = meshRaw;
  }

  // Renders that hit a not-yet-uploaded mesh are warmups: AssetPreviewViewport's
  // onRenderPass kicks off the GPU upload via a one-time copy pass that runs
  // at the start of the NEXT frame. The render this frame draws with garbage
  // GPU vertex state. Don't mark the entry as rendered or persist anything;
  // the cache will re-enter renderAndPersist next frame when isLoaded() is
  // true and the upload has actually happened.
  bool wasLoaded = asset.mesh3D->isLoaded();

  ImVec2 renderSize{(float)THUMB_PERSIST_PX, (float)THUMB_PERSIST_PX};
  entry.viewport->renderHeadless(renderSize);
  if (!entry.viewport->getTexture()) return wasLoaded ? false : true;

  if (!wasLoaded) return true; // budget consumed; warmup only

  entry.dirty = false;
  entry.everRendered = true;
  // Defer the readPixels+save by one frame. readPixels here would capture
  // the framebuffer BEFORE this frame's queued render pass executes (UI
  // build happens before GPU passes), so the saved PNG would be one frame
  // stale — empty for first touches.
  entry.persistDeferred = true;
  return true;
}

SDL_GPUTexture* Editor::ModelThumbnailCache::fetch(
  const Project::AssetManagerEntry &asset)
{
  uint64_t uuid = asset.getUUID();
  auto it = entries.find(uuid);
  if (it == entries.end()) {
    auto ent = std::make_unique<Entry>();
    tryLoadFromDisk(uuid, *ent);
    it = entries.emplace(uuid, std::move(ent)).first;
  }
  auto &entry = *it->second;

  // Pending persist from the previous frame: by now the queued render has
  // executed and the framebuffer holds the actual content, so readPixels
  // sees the rendered mesh rather than the pre-render clear.
  if (entry.persistDeferred && entry.viewport) {
    std::vector<uint8_t> rgba{};
    uint32_t w = 0, h = 0;
    if (entry.viewport->readPixels(rgba, w, h)) {
      auto pngPath = thumbPath(uuid);
      if (!pngPath.empty() && !savePngFromBytes(pngPath, rgba, w, h)) {
        Utils::Logger::log(
          "ModelThumbnailCache: IMG_SavePNG failed for " + pngPath.string(),
          Utils::Logger::LEVEL_WARN
        );
      }
    }
    entry.persistDeferred = false;
  }

  bool needRender = entry.dirty || (!entry.everRendered && !entry.loadedTex);
  if (needRender && rendersThisFrame < MAX_PER_FRAME) {
    if (renderAndPersist(asset, entry)) {
      ++rendersThisFrame;
    }
  }

  if (entry.loadedTex) return entry.loadedTex->getGPUTex();
  if (entry.everRendered && entry.viewport) return entry.viewport->getTexture();
  return nullptr;
}

void Editor::ModelThumbnailCache::invalidate(uint64_t uuid)
{
  auto it = entries.find(uuid);
  if (it != entries.end()) {
    auto &entry = *it->second;
    entry.dirty = true;
    entry.loadedTex.reset();
  }
  auto pngPath = thumbPath(uuid);
  if (!pngPath.empty()) {
    std::error_code ec;
    fs::remove(pngPath, ec);
  }
}

void Editor::ModelThumbnailCache::erase(uint64_t uuid)
{
  entries.erase(uuid);
  auto pngPath = thumbPath(uuid);
  if (!pngPath.empty()) {
    std::error_code ec;
    fs::remove(pngPath, ec);
  }
}

void Editor::ModelThumbnailCache::clear()
{
  entries.clear();
}
