// Drag-drop payload definitions for prefab editor → graph canvas drops.
// Producers: PrefabEditor's My-Prefab variables / functions panels.
// Consumers: PrefabEventGraphEditor's canvas drop target. Keep these POD
// structs trivially-copyable — ImGui's drag-drop API stores payloads via raw
// memcpy and re-emits via raw access.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace Editor::DragDrop
{
  // ImGui drag-drop "type" strings. Max 32 chars (ImGui limit).
  inline constexpr const char* TYPE_PREFAB_VAR    = "P64_VAR";
  inline constexpr const char* TYPE_PREFAB_FUNC   = "P64_FUNC";
  // WidgetBlueprintEditor palette source. Canvas-mode Viewport2D consumes
  // this and spawns a fresh Object with the requested component attached.
  inline constexpr const char* TYPE_WIDGET_PALETTE = "P64_WIDGET_PAL";

  struct WidgetPalettePayload
  {
    uint32_t componentID{0};
  };

  struct PrefabVarPayload
  {
    uint64_t uuid{0};
    uint8_t  kind{0};
    char     name[64]{};
  };

  struct PrefabFuncPayload
  {
    char name[64]{};
  };

  // Helper: write a std::string into a fixed-size payload name buffer,
  // truncating safely so the destination is always NUL-terminated.
  inline void copyName(char *dst, size_t cap, const std::string &src)
  {
    if (cap == 0) return;
    size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
  }
}
