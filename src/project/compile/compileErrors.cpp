#include "compileErrors.h"

namespace Project::Compile
{
  void ErrorList::clear()
  {
    entries.clear();
    ++revision;
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
