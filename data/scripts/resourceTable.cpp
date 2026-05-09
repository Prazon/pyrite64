// AUTO-GENERATED FILE. Do not edit by hand.
#include <stdint.h>
#include <stddef.h>
#include <assets/resourceTable.h>
#include <assets/assetManager.h>

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

  // Editor-authored field tables. One row per field, sorted by declaration
  // order (matches the binary blob layout the resource builder emitted).
  // Header-authored types reference no rows (count=0, ptr=nullptr).
  struct ResourceFieldRow {
    uint64_t uuid;
    uint8_t  kind;
    uint16_t offset;
    uint16_t size;
  };

__RESOURCE_FIELD_TABLES__

  struct ResourceTypeFields {
    const ResourceFieldRow* rows;
    uint16_t count;
  };

  const ResourceTypeFields typeFields[] = {
__RESOURCE_TYPE_FIELD_ROWS__
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

  const ResourceFieldRow* findRow(uint8_t typeIdx, uint64_t fieldUuid)
  {
    if (typeIdx >= TYPE_TABLE_COUNT) return nullptr;
    const auto &tf = typeFields[typeIdx];
    for (uint16_t i = 0; i < tf.count; ++i) {
      if (tf.rows[i].uuid == fieldUuid) return &tf.rows[i];
    }
    return nullptr;
  }

  // The resource builder emitted multibyte fields with byteswap (see
  // BinaryFile::write), so reads here perform the reverse swap. The N64
  // is big-endian native, so loads on aligned data produce the original
  // value without intermediate copy.
  uint8_t  loadU8 (const uint8_t* p) { return p[0]; }
  uint16_t loadU16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
  uint32_t loadU32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
  }
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

void* P64::Resources::getFieldPtr(uint32_t assetIdx, uint64_t fieldUuid)
{
  if (assetIdx >= TYPE_FOR_ASSET_IDX_COUNT) return nullptr;
  uint8_t typeIdx = typeForAssetIdx[assetIdx];
  const auto* row = findRow(typeIdx, fieldUuid);
  if (!row) return nullptr;
  void* data = P64::AssetManager::getByIndex(assetIdx);
  if (!data) return nullptr;
  return (uint8_t*)data + row->offset;
}

int32_t P64::Resources::getS32(uint32_t assetIdx, uint64_t fieldUuid)
{
  void* p = getFieldPtr(assetIdx, fieldUuid);
  if (!p) return 0;
  return (int32_t)loadU32((const uint8_t*)p);
}

float P64::Resources::getF32(uint32_t assetIdx, uint64_t fieldUuid)
{
  void* p = getFieldPtr(assetIdx, fieldUuid);
  if (!p) return 0.0f;
  uint32_t bits = loadU32((const uint8_t*)p);
  float f;
  __builtin_memcpy(&f, &bits, sizeof(f));
  return f;
}

bool P64::Resources::getBool(uint32_t assetIdx, uint64_t fieldUuid)
{
  void* p = getFieldPtr(assetIdx, fieldUuid);
  if (!p) return false;
  return loadU8((const uint8_t*)p) != 0;
}

uint32_t P64::Resources::getRef(uint32_t assetIdx, uint64_t fieldUuid)
{
  void* p = getFieldPtr(assetIdx, fieldUuid);
  if (!p) return 0;
  return loadU32((const uint8_t*)p);
}
