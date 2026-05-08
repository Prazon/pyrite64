// AUTO-GENERATED FILE. Do not edit by hand.
#include <stdint.h>
#include <stddef.h>
#include <assets/resourceTable.h>

namespace P64::Asset
{
__RESOURCE_DECL__
}

namespace
{
  using FuncResource = void(*)(void*);
  struct ResourceTypeEntry {
    FuncResource onLoad;
    FuncResource onUnload;
  };

  ResourceTypeEntry typeTable[] = {
__RESOURCE_TYPE_ENTRIES__
  };

  // Maps each entry in the rom asset table to the resource type-table index
  // (or 0xFF when the entry isn't a resource). Sized to the asset table so
  // bounds checks reuse `assetIdx < sizeof(typeForAssetIdx)`.
  const uint8_t typeForAssetIdx[] = {
__RESOURCE_TYPE_FOR_IDX__
  };
  constexpr size_t TYPE_FOR_ASSET_IDX_COUNT =
    sizeof(typeForAssetIdx) / sizeof(typeForAssetIdx[0]);
  constexpr size_t TYPE_TABLE_COUNT =
    sizeof(typeTable) / sizeof(typeTable[0]);
}

void P64::Resources::callOnLoad(uint32_t assetIdx, void* data)
{
  if (!data) return;
  if (assetIdx >= TYPE_FOR_ASSET_IDX_COUNT) return;
  uint8_t typeIdx = typeForAssetIdx[assetIdx];
  if (typeIdx >= TYPE_TABLE_COUNT) return;
  auto fn = typeTable[typeIdx].onLoad;
  if (fn) fn(data);
}

void P64::Resources::callOnUnload(uint32_t assetIdx, void* data)
{
  if (!data) return;
  if (assetIdx >= TYPE_FOR_ASSET_IDX_COUNT) return;
  uint8_t typeIdx = typeForAssetIdx[assetIdx];
  if (typeIdx >= TYPE_TABLE_COUNT) return;
  auto fn = typeTable[typeIdx].onUnload;
  if (fn) fn(data);
}
