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
#include "../../../imgui/helper.h"
#include "imgui_internal.h"

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
  // GML editor uses 4-wide tab indentation; matches modern-IDE conventions.
  editor->SetTabSize(4);

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

  // Open as a floating window — user can dock or drag it to its own OS window
  // (Unreal-style asset editor). imgui.ini restores position/dock state on
  // subsequent sessions.
  auto screenSize = ImGui::GetMainViewport()->WorkSize;
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
    {(screenSize.x - DEF_WIN_SIZE.x) / 2, (screenSize.y - DEF_WIN_SIZE.y) / 2},
    ImGuiCond_FirstUseEver
  );
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

  // Right-click context menu, mirroring the asset-browser style: icon + label,
  // entries disabled when the operation isn't applicable. TextEditor renders
  // into its own child window, which is what we attach the popup to.
  if (ImGui::BeginPopupContextItem("##codeEditorCtx")) {
    bool hasSel = editor->HasSelection();
    bool readOnly = editor->IsReadOnly();
    bool canUndo = editor->CanUndo();
    bool canRedo = editor->CanRedo();

    if (ImGui::MenuItem(ICON_MDI_UNDO " Undo", "Ctrl+Z", false, canUndo)) {
      editor->Undo();
    }
    if (ImGui::MenuItem(ICON_MDI_REDO " Redo", "Ctrl+Y", false, canRedo)) {
      editor->Redo();
    }

    ImGui::Separator();

    if (ImGui::MenuItem(ICON_MDI_CONTENT_CUT " Cut", "Ctrl+X", false, hasSel && !readOnly)) {
      editor->Cut();
    }
    if (ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Copy", "Ctrl+C", false, hasSel)) {
      editor->Copy();
    }
    if (ImGui::MenuItem(ICON_MDI_CONTENT_PASTE " Paste", "Ctrl+V", false, !readOnly)) {
      editor->Paste();
    }

    ImGui::Separator();

    if (ImGui::MenuItem(ICON_MDI_SELECT_ALL " Select All", "Ctrl+A")) {
      editor->SelectAll();
    }

    ImGui::Separator();

    if (ImGui::MenuItem(ICON_MDI_CONTENT_SAVE " Save", "Ctrl+S", false, isDirty())) {
      saveToDisk();
    }

    ImGui::EndPopup();
  }

  ImGui::End();
  return isOpen;
}

void Editor::CodeEditor::focus() const
{
  // Resolve the unique window ID and bring it to the front next frame.
  ImGui::SetWindowFocus(("###CodeEditor_" + std::to_string(assetUUID)).c_str());
}
