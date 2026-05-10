/**
 * @copyright 2026 - Prazon
 * @license MIT
 */
#include "matThumbnailCache.h"

#include <filesystem>
#include <system_error>

#include <SDL3/SDL.h>
#include "SDL3_image/SDL_image.h"

#include "../../../../context.h"
#include "../../../../project/assetManager.h"
#include "../../../../project/cacheDir.h"
#include "../../../../project/project.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"

namespace fs = std::filesystem;

namespace
{
  constexpr const char *kSubsystem = "matThumb";

  fs::path thumbPath(uint64_t uuid)
  {
    if (!ctx.project) return {};
    return Project::Cache::fileFor(*ctx.project, kSubsystem,
                                   std::to_string(uuid) + ".png");
  }

  // True when the source asset file is newer than (or the same age as) the
  // cached PNG, indicating the cache is stale. A missing PNG also counts as
  // stale. A missing source asset is treated as fresh — we have no basis for
  // invalidating, and the caller will skip caching anyway.
  bool isCacheStale(uint64_t uuid)
  {
    auto pngPath = thumbPath(uuid);
    if (pngPath.empty()) return true;
    std::error_code ec;
    if (!fs::exists(pngPath, ec)) return true;

    if (!ctx.project) return true;
    auto *entry = ctx.project->getAssets().getEntryByUUID(uuid);
    if (!entry) return false;

    // getFileAge returns the file's mtime as time-since-epoch (larger ==
    // newer). The PNG is stale when the source asset has a newer mtime.
    auto pngTs = Utils::FS::getFileAge(pngPath);
    auto srcTs = Utils::FS::getFileAge(entry->path);
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

bool Editor::MaterialThumbnailCache::tryLoadFromDisk(uint64_t uuid, Entry &entry)
{
  auto pngPath = thumbPath(uuid);
  if (pngPath.empty()) return false;
  std::error_code ec;
  if (!fs::exists(pngPath, ec)) return false;
  if (isCacheStale(uuid)) return false;
  if (!ctx.gpu) return false;

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
      std::string("MaterialThumbnailCache: failed to load cached PNG ")
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

bool Editor::MaterialThumbnailCache::renderAndPersist(
  uint64_t uuid, ImVec2 size,
  const Project::Assets::Material &mat,
  Entry &entry)
{
  if (!entry.viewport) {
    entry.viewport = std::make_unique<MaterialPreviewViewport>();
  }
  entry.viewport->setMaterial(mat);

  // The first render of any material kicks off the host cube's GPU upload via
  // a one-time copy pass that runs at the start of the NEXT frame; the render
  // queued this frame draws with un-uploaded vertex data and produces an
  // empty framebuffer. Skip persisting on warmup frames — once the host is
  // uploaded the next renderAndPersist call captures a real image.
  bool wasHostUploaded = entry.viewport->isHostUploaded();

  ImVec2 renderSize{(float)THUMB_PERSIST_PX, (float)THUMB_PERSIST_PX};
  auto *tex = entry.viewport->renderHeadless(renderSize);
  (void)size; // size kept in the API for parity but ignored — see above.

  if (!tex) return wasHostUploaded ? false : true;
  if (!wasHostUploaded) return true; // budget consumed; warmup only

  entry.dirty = false;
  entry.everRendered = true;
  // Defer the readPixels+save by one frame: readPixels here would capture
  // the framebuffer BEFORE this frame's queued render pass executes.
  entry.persistDeferred = true;
  return true;
}

SDL_GPUTexture* Editor::MaterialThumbnailCache::fetch(
  uint64_t uuid, ImVec2 size, const Project::Assets::Material &mat)
{
  auto it = entries.find(uuid);
  if (it == entries.end()) {
    auto ent = std::make_unique<Entry>();
    // First touch: try the persisted PNG before queuing a render. A hit means
    // we never construct a viewport for this material, which is a meaningful
    // memory + GPU-resource saving on big projects.
    tryLoadFromDisk(uuid, *ent);
    it = entries.emplace(uuid, std::move(ent)).first;
  }
  auto &entry = *it->second;

  // Pending persist from the previous frame: by now the queued render has
  // executed and the framebuffer holds the actual content, so readPixels
  // sees the rendered material rather than the pre-render clear.
  if (entry.persistDeferred && entry.viewport) {
    std::vector<uint8_t> rgba{};
    uint32_t w = 0, h = 0;
    if (entry.viewport->readPixels(rgba, w, h)) {
      auto pngPath = thumbPath(uuid);
      if (!pngPath.empty() && !savePngFromBytes(pngPath, rgba, w, h)) {
        Utils::Logger::log(
          "MaterialThumbnailCache: IMG_SavePNG failed for " + pngPath.string(),
          Utils::Logger::LEVEL_WARN
        );
      }
    }
    entry.persistDeferred = false;
  }

  bool needRender = entry.dirty || (!entry.everRendered && !entry.loadedTex);
  if (needRender && rendersThisFrame < MAX_PER_FRAME) {
    if (renderAndPersist(uuid, size, mat, entry)) {
      ++rendersThisFrame;
    }
  }

  if (entry.loadedTex) return entry.loadedTex->getGPUTex();
  if (entry.everRendered && entry.viewport) return entry.viewport->getTexture();
  return nullptr;
}

void Editor::MaterialThumbnailCache::invalidate(uint64_t uuid)
{
  auto it = entries.find(uuid);
  if (it != entries.end()) {
    auto &entry = *it->second;
    entry.dirty = true;
    // Drop any disk-loaded texture; the next fetch must re-render to reflect
    // the user's edit. A live viewport stays so the existing GPU texture
    // remains valid for the asset browser this frame.
    entry.loadedTex.reset();
  }

  auto pngPath = thumbPath(uuid);
  if (!pngPath.empty()) {
    std::error_code ec;
    fs::remove(pngPath, ec);
  }
}

void Editor::MaterialThumbnailCache::erase(uint64_t uuid)
{
  entries.erase(uuid);
  auto pngPath = thumbPath(uuid);
  if (!pngPath.empty()) {
    std::error_code ec;
    fs::remove(pngPath, ec);
  }
}

void Editor::MaterialThumbnailCache::clear()
{
  entries.clear();
}
