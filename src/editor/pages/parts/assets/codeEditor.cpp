/**
* SPBF64 fork: in-editor C++ source editor window. Wraps ImGuiColorTextEdit
* (vendored) and persists changes to disk via the file path resolved from
* the asset UUID. Find/replace + undo/redo come from TextEditor itself.
*/
#include "codeEditor.h"

#include <fstream>
#include <sstream>

#include "../../../../context.h"
#include "../../../../utils/fs.h"
#include "../../../../utils/logger.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{720, 600};
}

Editor::CodeEditor::CodeEditor(uint64_t uuid) : assetUUID(uuid)
{
  editor = std::make_unique<TextEditor>();
  editor->SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
  editor->SetPalette(TextEditor::GetDarkPalette());
  editor->SetShowWhitespaces(false);
  editor->SetTabSize(2);

  loadFromDisk();
}

void Editor::CodeEditor::loadFromDisk()
{
  if (!ctx.project) return;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset) return;

  filePath = asset->path;
  std::ifstream f(filePath);
  if (!f.is_open()) {
    editor->SetText("");
    savedText.clear();
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string text = ss.str();
  editor->SetText(text);
  savedText = text;
}

void Editor::CodeEditor::saveToDisk()
{
  if (filePath.empty()) return;
  std::string text = editor->GetText();
  // TextEditor::GetText always appends a trailing newline; strip the duplicate
  // if the source didn't end with one and we don't want to introduce drift.
  Utils::FS::saveTextFile(filePath, text);
  savedText = text;
  Utils::Logger::log("Saved: " + filePath);
}

bool Editor::CodeEditor::isDirty() const
{
  return editor->GetText() != savedText;
}

bool Editor::CodeEditor::draw(ImGuiID defDockId)
{
  if (!ctx.project) return false;
  auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!asset) return false;

  // Title shows the file name + asterisk for unsaved changes. Window ID is
  // tied to the UUID so multiple files can be open simultaneously without
  // ImGui ID collisions.
  std::string baseTitle = "Code: " + asset->name + (isDirty() ? " *" : "");
  winName = baseTitle + "###CodeEditor_" + std::to_string(assetUUID);

  if (dockOnFirstAppearance) {
    ImGui::SetNextWindowDockID(defDockId, ImGuiCond_Always);
    dockOnFirstAppearance = false;
  } else {
    ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  }
  if (forceFocusNextFrame) {
    ImGui::SetNextWindowFocus();
    forceFocusNextFrame = false;
  }

  bool isOpen = true;
  if (!ImGui::Begin(winName.c_str(), &isOpen,
        isDirty() ? ImGuiWindowFlags_UnsavedDocument : 0))
  {
    ImGui::End();
    return isOpen;
  }

  // Ctrl+S → save (only when this window has focus, so we don't capture
  // shortcuts intended for other dock tabs).
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      saveToDisk();
    }
  }

  // Toolbar
  if (ImGui::Button("Save")) saveToDisk();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", filePath.c_str());

  // Editor body — fills the rest of the window.
  editor->Render("##editor", ImVec2(0, 0), false);

  ImGui::End();
  return isOpen;
}

void Editor::CodeEditor::focus() const
{
  // Resolve the unique window ID and bring it to the front next frame.
  ImGui::SetWindowFocus(("###CodeEditor_" + std::to_string(assetUUID)).c_str());
}
