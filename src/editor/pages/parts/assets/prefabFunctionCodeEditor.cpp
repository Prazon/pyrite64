#include "prefabFunctionCodeEditor.h"

#include "../../../../context.h"
#include "../../../../utils/logger.h"
#include "../../../../project/prefabFunctions.h"

#include "imgui_internal.h"

namespace
{
  constexpr ImVec2 DEF_WIN_SIZE{720, 600};
  // Match CodeEditor's source font sizing — readable for code without
  // touching the surrounding chrome / inspector density.
  constexpr float CODE_FONT_SIZE_PX = 16.0f;
}

Editor::PrefabFunctionCodeEditor::PrefabFunctionCodeEditor(
  uint64_t synthUUID,
  std::string prefabName_,
  std::string funcName_)
  : syntheticUUID(synthUUID),
    prefabName(std::move(prefabName_)),
    funcName(std::move(funcName_))
{
  editor = std::make_unique<TextEditor>();
  editor->SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
  editor->SetPalette(TextEditor::GetDarkPalette());
  editor->SetShowWhitespaces(false);
  editor->SetTabSize(4);
  loadFromDisk();
}

void Editor::PrefabFunctionCodeEditor::loadFromDisk()
{
  if (!ctx.project) return;
  std::string slice = Project::extractPrefabFunctionSource(
    ctx.project->getPath(), prefabName, funcName
  );
  if (slice.empty()) {
    // Function not found — surface as an empty editor so the user has
    // somewhere to type a new body, but log so it's not silent.
    Utils::Logger::log(
      "PrefabFunctionCodeEditor: function '" + funcName +
      "' not found in " + prefabName + ".cpp",
      Utils::Logger::LEVEL_WARN
    );
    editor->SetText("");
    savedText.clear();
    return;
  }
  editor->SetText(slice);
  savedText = slice;
}

void Editor::PrefabFunctionCodeEditor::saveToDisk()
{
  if (!ctx.project) return;
  std::string text = editor->GetText();
  // TextEditor::GetText always appends a trailing newline that we feed
  // back into the splice; replacePrefabFunctionSource preserves it.

  // Re-locate by the *current* funcName (the name we last saw on disk),
  // not the post-edit name from the slice — the file still has the old
  // name as long as we haven't saved.
  if (!Project::replacePrefabFunctionSource(
        ctx.project->getPath(), prefabName, funcName, text)) {
    Utils::Logger::log(
      "PrefabFunctionCodeEditor: failed to splice '" + funcName +
      "' back into " + prefabName + ".cpp (function range not found)",
      Utils::Logger::LEVEL_ERROR
    );
    return;
  }

  // Sync the .h declaration if the user changed the signature inline.
  std::string newName, retType, params;
  if (Project::parsePrefabFunctionSignatureFromSlice(
        text, newName, retType, params)) {
    if (!newName.empty()) {
      Project::updatePrefabFunctionHeader(
        ctx.project->getPath(), prefabName,
        funcName, newName, retType, params
      );
      // Track forward — next save needs to look up by the post-rename
      // name since the .cpp now has it.
      funcName = newName;
    }
  }

  savedText = text;
  Utils::Logger::log("Saved function: " + prefabName + "::" + funcName);
}

bool Editor::PrefabFunctionCodeEditor::isDirty() const
{
  return editor->GetText() != savedText;
}

bool Editor::PrefabFunctionCodeEditor::draw(ImGuiID defDockId)
{
  std::string baseTitle = funcName + " — " + prefabName + ".cpp"
                        + (isDirty() ? " *" : "");
  // ID suffix keyed off the synthetic UUID so multiple per-function
  // editors stay distinct in ImGui's id stack.
  winName = baseTitle + "###PrefabFnEditorWin_" + std::to_string(syntheticUUID);

  ImGuiWindowClass cls{};
  cls.ViewportFlagsOverrideSet   = ImGuiViewportFlags_NoAutoMerge;
  cls.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration;
  ImGui::SetNextWindowClass(&cls);

  // Same docking story as CodeEditor: a per-instance firstDockTarget
  // override beats the loop-passed default so PrefabEditor can drop us
  // next to its viewport. DockBuilderDockWindow + Always together beat
  // any stale imgui.ini layout from a previous session.
  if (firstDockTarget && !firstDockApplied) {
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

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveToDisk();
  }

  if (ImGui::Button("Save")) saveToDisk();
  ImGui::SameLine();
  ImGui::TextDisabled("%s::%s", prefabName.c_str(), funcName.c_str());

  ImGui::PushFont(nullptr, CODE_FONT_SIZE_PX);
  editor->Render("##editor", ImVec2(0, 0), false);
  ImGui::PopFont();

  ImGui::End();
  return isOpen;
}

void Editor::PrefabFunctionCodeEditor::focus() const
{
  ImGui::SetWindowFocus(
    ("###PrefabFnEditorWin_" + std::to_string(syntheticUUID)).c_str()
  );
}
