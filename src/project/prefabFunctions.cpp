#include "prefabFunctions.h"

#include <cstring>
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

std::string Project::sanitizePrefabIdent(std::string s)
{
  for (auto &c : s) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
          || (c >= '0' && c <= '9') || c == '_';
    if (!ok) c = '_';
  }
  if (s.empty() || (s[0] >= '0' && s[0] <= '9')) s.insert(s.begin(), '_');
  return s;
}

namespace
{
  // Generate (or read) the user-source pair for `prefabName`. Same template
  // AssetsBrowser uses on prefab creation — duplicated here so user code
  // edits work for prefabs that pre-date the scaffold (or had their files
  // deleted). Returns the pair of paths.
  std::pair<fs::path, fs::path> ensureUserSourcePair(
    const std::string &projectPath, const std::string &prefabName)
  {
    fs::path userDir = fs::path{projectPath} / "src" / "user";
    fs::create_directories(userDir);
    fs::path headerPath = userDir / (prefabName + ".h");
    fs::path sourcePath = userDir / (prefabName + ".cpp");

    std::error_code ec;
    std::string ident = Project::sanitizePrefabIdent(prefabName);

    if (!fs::exists(headerPath, ec)) {
      std::string headerStub =
        "// Per-prefab user code for \"" + prefabName + "\".\n"
        "// Functions tagged with P64_NODE are surfaced as nodes in the\n"
        "// prefab's event graph and dispatched from the runtime.\n"
        "#pragma once\n"
        "#include \"script/prefabNode.h\"\n"
        "#include \"p64/prefabVars.h\"\n\n"
        "namespace User::" + ident + " {\n"
        "}\n";
      Utils::FS::saveTextFile(headerPath.string(), headerStub);
    }
    if (!fs::exists(sourcePath, ec)) {
      std::string sourceStub =
        "#include \"" + prefabName + ".h\"\n\n"
        "namespace User::" + ident + " {\n"
        "}\n";
      Utils::FS::saveTextFile(sourcePath.string(), sourceStub);
    }
    return {headerPath, sourcePath};
  }

  // Insert `lineToInsert` right before the file's final `}` brace (the
  // closing brace of the namespace). Falls back to appending if no closing
  // brace is found. Preserves any trailing whitespace/newline by inserting
  // ahead of the brace's line. Single line, no extra blank-line hygiene —
  // trivial enough that we can keep this in one place.
  std::string insertBeforeLastBrace(const std::string &content, const std::string &lineToInsert)
  {
    auto pos = content.find_last_of('}');
    if (pos == std::string::npos) {
      // No brace — just append to end.
      std::string out = content;
      if (!out.empty() && out.back() != '\n') out.push_back('\n');
      out += lineToInsert;
      if (!lineToInsert.empty() && lineToInsert.back() != '\n') out.push_back('\n');
      return out;
    }
    // Find the start of the brace's line so the inserted line lands on
    // its own line just above.
    auto lineStart = content.rfind('\n', pos);
    size_t insertAt = (lineStart == std::string::npos) ? 0 : lineStart + 1;

    std::string out;
    out.reserve(content.size() + lineToInsert.size() + 1);
    out.append(content, 0, insertAt);
    out += lineToInsert;
    if (!lineToInsert.empty() && lineToInsert.back() != '\n') out.push_back('\n');
    out.append(content, insertAt, std::string::npos);
    return out;
  }
}

// Replace every occurrence of `<token>(` with `<replacement>(`. Token-bound
// on the right by '('; on the left by a non-identifier char (or start of
// file). Avoids partial-match false positives like `MyOldName_thing(` when
// renaming `MyOldName`.
static std::string replaceFunctionToken(
  const std::string &src,
  const std::string &token,
  const std::string &replacement)
{
  std::string out;
  out.reserve(src.size());
  size_t i = 0;
  while (i < src.size()) {
    if (i + token.size() <= src.size()
        && std::memcmp(src.data() + i, token.data(), token.size()) == 0
        && i + token.size() < src.size()
        && src[i + token.size()] == '(')
    {
      char prev = i == 0 ? ' ' : src[i - 1];
      bool prevIsIdent = (prev >= 'A' && prev <= 'Z')
                      || (prev >= 'a' && prev <= 'z')
                      || (prev >= '0' && prev <= '9')
                      || prev == '_';
      if (!prevIsIdent) {
        out += replacement;
        i += token.size();
        continue;
      }
    }
    out.push_back(src[i++]);
  }
  return out;
}

void Project::ensurePrefabUserSource(
  const std::string &projectPath,
  const std::string &prefabName)
{
  if (prefabName.empty()) return;
  (void)ensureUserSourcePair(projectPath, prefabName);
}

bool Project::renamePrefabFunction(
  const std::string &projectPath,
  const std::string &prefabName,
  const std::string &oldName,
  const std::string &newName)
{
  if (oldName.empty() || newName.empty() || oldName == newName) return false;
  fs::path userDir = fs::path{projectPath} / "src" / "user";
  fs::path headerPath = userDir / (prefabName + ".h");
  fs::path sourcePath = userDir / (prefabName + ".cpp");

  std::error_code ec;
  if (!fs::exists(headerPath, ec) && !fs::exists(sourcePath, ec)) return false;

  if (fs::exists(headerPath, ec)) {
    auto h = Utils::FS::loadTextFile(headerPath.string());
    h = replaceFunctionToken(h, oldName, newName);
    Utils::FS::saveTextFile(headerPath.string(), h);
  }
  if (fs::exists(sourcePath, ec)) {
    auto c = Utils::FS::loadTextFile(sourcePath.string());
    c = replaceFunctionToken(c, oldName, newName);
    Utils::FS::saveTextFile(sourcePath.string(), c);
  }
  return true;
}

bool Project::removePrefabFunction(
  const std::string &projectPath,
  const std::string &prefabName,
  const std::string &functionName)
{
  if (functionName.empty()) return false;
  fs::path userDir = fs::path{projectPath} / "src" / "user";
  fs::path headerPath = userDir / (prefabName + ".h");
  fs::path sourcePath = userDir / (prefabName + ".cpp");

  std::error_code ec;
  bool any = false;

  // Header: drop the line containing `P64_NODE ... <name>(...)`. We match
  // line-by-line so we don't touch surrounding declarations.
  if (fs::exists(headerPath, ec)) {
    auto h = Utils::FS::loadTextFile(headerPath.string());
    std::string out;
    size_t lineStart = 0;
    while (lineStart < h.size()) {
      size_t lineEnd = h.find('\n', lineStart);
      if (lineEnd == std::string::npos) lineEnd = h.size();
      std::string line = h.substr(lineStart, lineEnd - lineStart);
      bool drop = (line.find("P64_NODE") != std::string::npos)
               && (line.find(functionName + "(") != std::string::npos);
      if (!drop) {
        out += line;
        if (lineEnd < h.size()) out.push_back('\n');
      } else {
        any = true;
      }
      lineStart = (lineEnd < h.size()) ? lineEnd + 1 : lineEnd;
    }
    if (any) Utils::FS::saveTextFile(headerPath.string(), out);
  }

  // Source: find `<name>(` preceded by whitespace, walk back to the start
  // of the line, then walk forward to the matching `}` past the body. Only
  // commit the strip if brace nesting balances cleanly.
  if (fs::exists(sourcePath, ec)) {
    auto c = Utils::FS::loadTextFile(sourcePath.string());
    std::string token = functionName + "(";
    size_t pos = 0;
    while ((pos = c.find(token, pos)) != std::string::npos) {
      char prev = pos == 0 ? ' ' : c[pos - 1];
      bool prevIsIdent = (prev >= 'A' && prev <= 'Z')
                      || (prev >= 'a' && prev <= 'z')
                      || (prev >= '0' && prev <= '9')
                      || prev == '_';
      if (prevIsIdent) { ++pos; continue; }

      size_t lineStart = c.rfind('\n', pos);
      lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;

      size_t openBrace = c.find('{', pos);
      if (openBrace == std::string::npos) break;
      int depth = 0;
      size_t scan = openBrace;
      bool ok = false;
      for (; scan < c.size(); ++scan) {
        if (c[scan] == '{') ++depth;
        else if (c[scan] == '}') {
          if (--depth == 0) { ok = true; break; }
        }
      }
      if (!ok) break;
      // Include the trailing newline if present so we don't leave a bare
      // newline behind.
      size_t endPos = scan + 1;
      if (endPos < c.size() && c[endPos] == '\n') ++endPos;

      c.erase(lineStart, endPos - lineStart);
      any = true;
      pos = lineStart;
    }
    if (any) Utils::FS::saveTextFile(sourcePath.string(), c);
  }
  return any;
}

bool Project::addPrefabFunction(
  const std::string &projectPath,
  const std::string &prefabName,
  const std::string &functionName)
{
  if (functionName.empty()) return false;

  auto [headerPath, sourcePath] = ensureUserSourcePair(projectPath, prefabName);
  std::string ident = sanitizePrefabIdent(prefabName);

  // Header: append a tagged forward declaration just before the namespace
  // closing brace. Two-space indent matches the scaffold's house style.
  {
    std::string content = Utils::FS::loadTextFile(headerPath.string());
    std::string decl = "  P64_NODE void " + functionName + "(P64::Object* self);\n";
    content = insertBeforeLastBrace(content, decl);
    Utils::FS::saveTextFile(headerPath.string(), content);
  }

  // Source: append an empty implementation, also before the namespace
  // closing brace. Empty body so the user can fill it in their editor.
  {
    std::string content = Utils::FS::loadTextFile(sourcePath.string());
    std::string impl = "  void " + functionName + "(P64::Object* self) {\n  }\n";
    content = insertBeforeLastBrace(content, impl);
    Utils::FS::saveTextFile(sourcePath.string(), content);
  }
  return true;
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
