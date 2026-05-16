/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "projectTemplates.h"

#include "json.hpp"
#include "../utils/json.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace Project
{
  fs::path templatesRoot()
  {
    // Mirrors doProjectCreate / PROJECT_CREATE: a relative path resolved
    // against the binary's working dir (./n64 ships beside pyrite64).
    return fs::path("n64") / "examples";
  }

  std::vector<ProjectTemplate> listProjectTemplates()
  {
    std::vector<ProjectTemplate> out;

    std::error_code ec;
    auto root = templatesRoot();
    if (!fs::is_directory(root, ec)) return out;

    for (const auto &entry : fs::directory_iterator(root, ec)) {
      if (ec) break;
      if (!entry.is_directory()) continue;

      auto cfg = entry.path() / "project.p64proj";
      if (!fs::exists(cfg)) continue;

      ProjectTemplate t;
      t.id = entry.path().filename().string();
      t.dir = entry.path();

      auto j = Utils::JSON::loadFile(cfg);
      if (j.is_object()) {
        t.label = j.value("name", std::string{});
        t.description = j.value("description", std::string{});
      }
      if (t.id == "empty") {
        // The example's own name ("Pyrite64 Project") reads oddly as a
        // template label; present it as the canonical blank starting point.
        t.label = "Empty Project";
        if (t.description.empty()) {
          t.description = "A minimal project: one scene with a camera, "
                          "lights, and a single box.";
        }
      }
      if (t.label.empty()) t.label = t.id;
      out.push_back(std::move(t));
    }

    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
      if ((a.id == "empty") != (b.id == "empty")) return a.id == "empty";
      return a.label < b.label;
    });
    return out;
  }
}
