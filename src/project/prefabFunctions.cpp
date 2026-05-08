#include "prefabFunctions.h"

#include <filesystem>
#include <regex>

#include "../utils/fs.h"

namespace fs = std::filesystem;

namespace
{
  // Strip C and C++ comments so example lines like `// P64_NODE void Foo();`
  // in the scaffolded stub don't show up as real functions. Keeps the file's
  // line numbering by replacing each removed character with a space.
  std::string stripComments(const std::string &in)
  {
    std::string out = in;
    size_t i = 0;
    while (i < out.size()) {
      if (out[i] == '/' && i + 1 < out.size() && out[i+1] == '/') {
        while (i < out.size() && out[i] != '\n') {
          out[i] = (out[i] == '\t' || out[i] == '\r') ? out[i] : ' ';
          ++i;
        }
      } else if (out[i] == '/' && i + 1 < out.size() && out[i+1] == '*') {
        out[i] = ' '; out[i+1] = ' '; i += 2;
        while (i + 1 < out.size() && !(out[i] == '*' && out[i+1] == '/')) {
          if (out[i] != '\n') out[i] = ' ';
          ++i;
        }
        if (i + 1 < out.size()) { out[i] = ' '; out[i+1] = ' '; i += 2; }
      } else {
        ++i;
      }
    }
    return out;
  }

  int countLineUpTo(const std::string &s, size_t pos)
  {
    int line = 1;
    for (size_t i = 0; i < pos && i < s.size(); ++i) {
      if (s[i] == '\n') ++line;
    }
    return line;
  }
}

std::vector<Project::PrefabFunctionDesc>
Project::scanPrefabFunctions(const std::string &projectPath, const std::string &prefabName)
{
  std::vector<PrefabFunctionDesc> out;

  fs::path headerPath = fs::path{projectPath} / "src" / "user" / (prefabName + ".h");
  std::error_code ec;
  if (!fs::exists(headerPath, ec)) return out;

  std::string source = Utils::FS::loadTextFile(headerPath.string());
  if (source.empty()) return out;

  std::string clean = stripComments(source);

  // Match: P64_NODE <returnType> <name>(<params>)
  // - returnType is a (possibly multi-token) sequence of identifier-ish chars,
  //   pointers, references, namespaces. We capture it greedily up to the
  //   function name.
  // - params captures everything between the parens; the user can post-process.
  static const std::regex re(
    R"(P64_NODE\s+([A-Za-z_][A-Za-z0-9_:\s\*&<>,]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\))"
  );

  for (auto it = std::sregex_iterator(clean.begin(), clean.end(), re);
       it != std::sregex_iterator(); ++it)
  {
    const auto &m = *it;
    PrefabFunctionDesc d;
    d.returnType = m[1].str();
    d.name       = m[2].str();
    d.params     = m[3].str();
    d.line       = countLineUpTo(clean, m.position(0));

    // Trim trailing whitespace from returnType (regex captured greedily).
    while (!d.returnType.empty() && std::isspace((unsigned char)d.returnType.back())) {
      d.returnType.pop_back();
    }
    out.push_back(std::move(d));
  }
  return out;
}
