/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace Project
{
  // A new-project template, backed by one of the shipped example projects
  // under n64/examples/. `id` is the directory name (the value passed to
  // project-create / PROJECT_CREATE); `label`/`description` come from the
  // example's own project.p64proj for display.
  struct ProjectTemplate
  {
    std::string id;
    std::string label;
    std::string description;
    std::filesystem::path dir;
  };

  // Resolves the templates root (n64/examples) relative to the editor
  // binary's working directory, the same way project-create copies it.
  std::filesystem::path templatesRoot();

  // Every example dir that contains a project.p64proj, sorted with the
  // "empty" template first (relabeled "Empty Project"), the rest by label.
  // Always returns at least the empty entry when the tree is present.
  std::vector<ProjectTemplate> listProjectTemplates();
}
