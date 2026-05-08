/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Project
{
  class Scene;
  class Selection;
}

namespace Editor::UndoRedo
{
  struct Entry
  {
    std::string state{};
    std::string description{};
    std::vector<uint32_t> selection{};

    uint64_t getMemoryUsage() const {
      return state.capacity() + description.capacity()
      + sizeof(Entry) + selection.capacity() * sizeof(uint32_t);
    }
  };

  /**
   * Manages undo/redo history for a single edit context.
   *
   * SPBF64 fork: History is now instantiable. Each editable scene (the active
   * project scene, plus each open PrefabEditor) owns its own History so the
   * stacks don't interfere. The currently-active history (the one
   * markChanged/getHistory() routes to) is set by Editor::UndoRedo::EditScope.
   */
  class History
  {
    private:
      std::vector<std::unique_ptr<Entry>> undoStack;
      std::vector<std::unique_ptr<Entry>> redoStack;
      size_t maxHistorySize{100};

      // Bound by EditScope; identifies which scene/selection this history
      // operates on for begin()/end()/undo()/redo().
      Project::Scene* boundScene{nullptr};
      Project::Selection* boundSelection{nullptr};

      Project::Scene* snapshotScene{nullptr};
      std::vector<uint32_t> snapshotSelUUIDs{};
      std::string nextChangedReason{};
      std::optional<std::string> savedState{};

    public:
      void bind(Project::Scene* scene, Project::Selection* selection) {
        boundScene = scene;
        boundSelection = selection;
      }
      [[nodiscard]] Project::Scene* getBoundScene() const { return boundScene; }
      [[nodiscard]] Project::Selection* getBoundSelection() const { return boundSelection; }

      bool undo();
      bool redo();
      void clear();

      void markChanged(std::string reason) {
        nextChangedReason = std::move(reason);
      }

      void markSaved();
      [[nodiscard]] bool isDirty() const;

      uint32_t getUndoCount() const { return (uint32_t)undoStack.size(); }
      uint32_t getRedoCount() const { return (uint32_t)redoStack.size(); }

      void begin();
      void end();

      [[nodiscard]] bool canUndo() const { return undoStack.size() > 1; }
      [[nodiscard]] bool canRedo() const { return !redoStack.empty(); }

      [[nodiscard]] std::string getUndoDescription() const;
      [[nodiscard]] std::string getRedoDescription() const;

      void setMaxHistorySize(size_t size);
      uint64_t getMemoryUsage();
  };

  /**
   * Returns the History associated with the currently-active EditScope.
   * Falls back to the main (project-scene) history when no scope is active.
   */
  History& getHistory();

  /**
   * RAII guard that wraps a slice of editing work against a specific History,
   * Scene, and Selection. Calls History::begin() on construction and
   * History::end() on destruction, and pushes itself onto the active stack so
   * getHistory() and markChanged() route correctly while it is alive.
   *
   * Scopes nest: a PrefabEditor::draw() pushes a new scope; while it is on
   * the stack, edits route to the prefab's history. When it pops, edits route
   * back to whatever scope was active before (the main scene's, typically).
   */
  class EditScope
  {
    private:
      History& history;
      Project::Scene* prevScene;
      Project::Selection* prevSelection;
    public:
      EditScope(History& h, Project::Scene& scene, Project::Selection& selection);
      ~EditScope();

      EditScope(const EditScope&) = delete;
      EditScope& operator=(const EditScope&) = delete;
  };

  // The "main" history, used when no EditScope is active.
  History& getMainHistory();
}
