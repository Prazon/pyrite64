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
