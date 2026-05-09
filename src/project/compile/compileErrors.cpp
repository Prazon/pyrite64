#include "compileErrors.h"

#include <algorithm>

namespace Project::Compile
{
  void ErrorList::clear()
  {
    entries.clear();
    ++revision;
  }

  void ErrorList::clearForAsset(uint64_t assetUUID)
  {
    if(assetUUID == 0) return;
    auto before = entries.size();
    entries.erase(
      std::remove_if(entries.begin(), entries.end(),
        [assetUUID](const Error &e){ return e.assetUUID == assetUUID; }),
      entries.end());
    if(entries.size() != before) ++revision;
  }

  void ErrorList::push(Error e)
  {
    entries.push_back(std::move(e));
    ++revision;
  }

  size_t ErrorList::errorCount() const
  {
    size_t n = 0;
    for (const auto &e : entries) if (e.severity == Severity::ERROR) ++n;
    return n;
  }

  size_t ErrorList::warningCount() const
  {
    size_t n = 0;
    for (const auto &e : entries) if (e.severity == Severity::WARNING) ++n;
    return n;
  }
}
