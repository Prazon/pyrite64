/**
 * @copyright 2026 - Prazon
 * @license MIT
 */
#include "matThumbnailCache.h"

SDL_GPUTexture* Editor::MaterialThumbnailCache::fetch(
  uint64_t uuid, ImVec2 size, const Project::Assets::Material &mat)
{
  auto it = entries.find(uuid);
  if (it == entries.end()) {
    auto ent = std::make_unique<Entry>();
    it = entries.emplace(uuid, std::move(ent)).first;
  }
  auto &entry = *it->second;

  bool needRender = entry.dirty || !entry.everRendered;
  if (needRender && rendersThisFrame < MAX_PER_FRAME) {
    entry.viewport.setMaterial(mat);
    auto* tex = entry.viewport.renderHeadless(size);
    if (tex) {
      entry.dirty = false;
      entry.everRendered = true;
      ++rendersThisFrame;
    }
  }

  return entry.everRendered ? entry.viewport.getTexture() : nullptr;
}

void Editor::MaterialThumbnailCache::invalidate(uint64_t uuid)
{
  auto it = entries.find(uuid);
  if (it != entries.end()) it->second->dirty = true;
}

void Editor::MaterialThumbnailCache::erase(uint64_t uuid)
{
  entries.erase(uuid);
}

void Editor::MaterialThumbnailCache::clear()
{
  entries.clear();
}
