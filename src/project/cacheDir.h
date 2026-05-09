/**
 * @copyright 2026 - Prazon
 * @license MIT
 *
 * Per-project cache directory helpers. Resolves <project>/.cache/<subsystem>/
 * paths and lazily creates the directories on first use. The directory is
 * gitignored at the project template level so cached artefacts (material
 * thumbnails, restored editor tabs, etc.) don't leak into version control.
 */
#pragma once
#include <filesystem>
#include <string_view>

namespace Project
{
  class Project;

  namespace Cache
  {
    // Returns <project>/.cache/<subsystem>/, creating the parent and the
    // subsystem directory if needed. Returns empty path if `project` is null.
    std::filesystem::path dirFor(const Project &project, std::string_view subsystem);

    // Returns <project>/.cache/<subsystem>/<file>. Ensures the directory
    // exists; the file itself is not touched.
    std::filesystem::path fileFor(const Project &project,
                                  std::string_view subsystem,
                                  std::string_view file);
  }
}
