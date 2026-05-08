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
  // Render the editor body at this unscaled pixel size regardless of the
  // global UI font (which is tuned for inspector density). Larger than the
  // surrounding chrome so source is comfortable to read. Pass as the second
  // arg to PushFont(nullptr, ...) — using GetFontSize() here would double-
  // apply ImGui's global scaling.
  constexpr float CODE_FONT_SIZE_PX = 16.0f;
}

// Editor formatting matches Unreal Engine's C++ coding standard:
//   - Tab character (not spaces) for indentation — TextEditor::EnterCharacter
//     inserts '\t' on the Tab key.
//   - Tab width 4 (SetTabSize below).
//   - Auto-indent on Enter copies the previous line's leading whitespace, so
//     pressing Enter inside an indented block keeps the same level. Inherited
//     from the C++ LanguageDefinition (mAutoIndentation = true).
// Brace style / wrapping is a code-authoring concern, not an editor concern —
// the editor preserves whatever you type, which is what Unreal style needs.
Editor::CodeEditor::CodeEditor(uint64_t uuid) : assetUUID(uuid)
{
  editor = std::make_unique<TextEditor>();
  editor->SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
  editor->SetPalette(TextEditor::GetDarkPalette());
  editor->SetShowWhitespaces(false);
  editor->SetTabSize(4);

  loadFromDisk();
}

Editor::CodeEditor::CodeEditor(uint64_t syntheticUUID, std::string absolutePath)
  : assetUUID(syntheticUUID), pathOverride(std::move(absolutePath))
{
  editor = std::make_unique<TextEditor>();
  editor->SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
  editor->SetPalette(TextEditor::GetDarkPalette());
  editor->SetShowWhitespaces(false);
  editor->SetTabSize(4);

  // Display name = filename (asset->name analogue) so the tab label reads
  // naturally for files that have no AssetManagerEntry.
  auto slash = pathOverride.find_last_of("/\\");
  nameOverride = (slash == std::string::npos)
    ? pathOverride
    : pathOverride.substr(slash + 1);

  loadFromDisk();
}

void Editor::CodeEditor::loadFromDisk()
{
  if (!pathOverride.empty()) {
    filePath = pathOverride;
  } else {
    if (!ctx.project) return;
    auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
    if (!asset) return;
    filePath = asset->path;
  }

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
  std::string displayName;
  if (!pathOverride.empty()) {
    displayName = nameOverride;
  } else {
    if (!ctx.project) return false;
    auto *asset = ctx.project->getAssets().getEntryByUUID(assetUUID);
    if (!asset) return false;
    displayName = asset->name;
  }

  // Title shows just the file name + asterisk for unsaved changes — when
  // the code editor is docked as a tab alongside the prefab editor's
  // viewport, the parent editor already provides the prefab context, so a
  // "Code: Player.cpp" prefix would just bloat the tab. Window ID is tied
  // to the UUID so multiple files stay distinct in ImGui's id stack.
  std::string baseTitle = displayName + (isDirty() ? " *" : "");
  // ID suffix bumped (was ###CodeEditor_) so stale imgui.ini entries from
  // the old auto-dock-into-3D-Viewport behavior don't override our new
  // spawn-as-its-own-OS-window default.
  winName = baseTitle + "###CodeEditorWin_" + std::to_string(assetUUID);

  // Dock as a sibling tab of Scene Editor; OS chrome on undock — see
  // PrefabEditor::draw for rationale.
  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  // Caller-supplied first-frame dock override wins over the loop-passed
  // default. PrefabEditor uses this to drop function source tabs next to
  // its viewport. Use Always when the override is in play so a stale
  // imgui.ini entry from when this file was previously opened standalone
  // doesn't keep it floating; FirstUseEver is fine for the loop-passed
  // default since that path doesn't have to fight prior layout state.
  if (firstDockTarget && !firstDockApplied) {
    // Seed the docking layout directly. SetNextWindowDockID alone loses to
    // a stale imgui.ini entry that already placed this winName into another
    // node when the file was previously opened standalone — the loaded
    // Window record wins on the very first frame. DockBuilderDockWindow
    // mutates the layout before the window registers, which beats both.
    ImGui::DockBuilderDockWindow(winName.c_str(), firstDockTarget);
    ImGui::SetNextWindowDockID(firstDockTarget, ImGuiCond_Always);
    firstDockApplied = true;
  } else if (defDockId) {
    ImGui::SetNextWindowDockID(defDockId, ImGuiCond_FirstUseEver);
  }

  auto *mvp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
    {
      mvp->Pos.x + (mvp->Size.x - DEF_WIN_SIZE.x) * 0.5f,
      mvp->Pos.y + (mvp->Size.y - DEF_WIN_SIZE.y) * 0.5f,
    },
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

  // Editor body — fills the rest of the window. Push a larger font for the
  // source view only; the surrounding window chrome (toolbar, tab bar) keeps
  // the global UI size. ImGui 1.92 PushFont(nullptr, sz) keeps the current
  // font face and overrides its unscaled size.
  ImGui::PushFont(nullptr, CODE_FONT_SIZE_PX);
  editor->Render("##editor", ImVec2(0, 0), false);
  ImGui::PopFont();

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
  ImGui::SetWindowFocus(("###CodeEditorWin_" + std::to_string(assetUUID)).c_str());
}
