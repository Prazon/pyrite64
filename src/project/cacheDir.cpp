/**
 * @copyright 2026 - Prazon
 * @license MIT
 */
#include "cacheDir.h"

#include "project.h"

namespace Project::Cache
{
  std::filesystem::path dirFor(const Project &project, std::string_view subsystem)
  {
    const auto &projectPath = project.getPath();
    if (projectPath.empty()) return {};

    std::filesystem::path dir = std::filesystem::path(projectPath) / ".cache" / subsystem;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
  }

  std::filesystem::path fileFor(const Project &project,
                                std::string_view subsystem,
                                std::string_view file)
  {
    auto dir = dirFor(project, subsystem);
    if (dir.empty()) return {};
    return dir / file;
  }
}
