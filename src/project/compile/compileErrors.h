#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Project::Compile
{
  enum class Severity : uint8_t
  {
    ERROR,
    WARNING,
  };

  struct Error
  {
    Severity    severity{Severity::ERROR};
    uint64_t    assetUUID{0};
    uint64_t    nodeUUID{0};
    std::string message{};
  };

  class ErrorList
  {
   public:
    void clear();
    // Drops only entries belonging to `assetUUID` and bumps the revision.
    // Used by the per-asset Compile button so refreshing one graph's
    // diagnostics doesn't nuke errors collected for unrelated assets by
    // a prior full project build or another open editor.
    void clearForAsset(uint64_t assetUUID);
    void push(Error e);

    const std::vector<Error>& all() const { return entries; }
    size_t errorCount()   const;
    size_t warningCount() const;

    uint32_t getRevision() const { return revision; }

   private:
    std::vector<Error> entries{};
    uint32_t           revision{0};
  };
}
