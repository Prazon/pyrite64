/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "undoRedo.h"

#include "../context.h"
#include "../project/scene/scene.h"
#include "../project/selection.h"

namespace
{
  Editor::UndoRedo::History mainHistory{};

  // SPBF64 fork: stack of active EditScopes. Top entry is the History
  // markChanged()/getHistory() target. Empty stack falls through to the
  // main history.
  std::vector<Editor::UndoRedo::History*> activeStack{};
}

namespace Editor::UndoRedo
{
  bool History::undo()
  {
    if (!canUndo()) return false;
    if (!boundScene || !boundSelection) return false;

    auto cmd = std::move(undoStack.back());
    undoStack.pop_back();
    const auto &prevCmd = undoStack.back();

    boundScene->deserialize(prevCmd->state);

    uint32_t primarySel = prevCmd->selection.empty() ? 0 : prevCmd->selection.back();
    boundSelection->setList(prevCmd->selection, primarySel);
    boundSelection->sanitize(boundScene);

    redoStack.push_back(std::move(cmd));

    return true;
  }

  bool History::redo()
  {
    if (!canRedo()) return false;
    if (!boundScene || !boundSelection) return false;

    auto cmd = std::move(redoStack.back());
    redoStack.pop_back();

    boundScene->deserialize(cmd->state);

    uint32_t primarySel = cmd->selection.empty() ? 0 : cmd->selection.back();
    boundSelection->setList(cmd->selection, primarySel);
    boundSelection->sanitize(boundScene);

    undoStack.push_back(std::move(cmd));

    return true;
  }

  void History::clear()
  {
    undoStack.clear();
    redoStack.clear();
    nextChangedReason.clear();
    savedState.reset();
    snapshotScene = nullptr;
    snapshotSelUUIDs.clear();
  }

  void History::begin() {
    if (!boundScene || !boundSelection) return;

    if (undoStack.empty()) {
      // First change: snapshot initial scene state so undo can return to it.
      std::string initialState = boundScene->serialize(true);
      if (!savedState.has_value()) {
        savedState = initialState;
      }
      auto ids = boundSelection->all();
      undoStack.push_back(std::make_unique<Entry>(
        std::move(initialState), "Initial State", ids
      ));
    }

    snapshotScene = boundScene;
    snapshotSelUUIDs = boundSelection->all();
  }

  void History::end() {
    if (nextChangedReason.empty()) return;

    auto scene = snapshotScene;
    snapshotScene = nullptr;
    if (!scene || !boundSelection) {
      nextChangedReason.clear();
      return;
    }

    if (!undoStack.empty()) {
      undoStack.back()->selection = snapshotSelUUIDs;
    }

    auto ids = boundSelection->all();

    auto newEntry = std::make_unique<Entry>(
      scene->serialize(true),
      nextChangedReason,
      ids
    );

    nextChangedReason.clear();

    if (!undoStack.empty()) {
      // check against last state to avoid pushing duplicate states
      if (undoStack.back()->state == newEntry->state
          && undoStack.back()->selection == newEntry->selection) {
        return;
      }
    }

    redoStack.clear();

    undoStack.push_back(std::move(newEntry));

    if (undoStack.size() > maxHistorySize) {
      undoStack.erase(undoStack.begin(), undoStack.end() - maxHistorySize);
    }
  }

  void History::markSaved()
  {
    if (undoStack.empty()) {
      savedState.reset();
      return;
    }

    savedState = undoStack.back()->state;
  }

  bool History::isDirty() const
  {
    if (undoStack.empty()) {
      return false;
    }

    if (!savedState.has_value()) {
      return false;
    }

    return undoStack.back()->state != *savedState;
  }

  std::string History::getUndoDescription() const
  {
    if (undoStack.empty()) return "";
    return undoStack.back()->description;
  }

  std::string History::getRedoDescription() const
  {
    if (redoStack.empty()) return "";
    return redoStack.back()->description;
  }

  void History::setMaxHistorySize(size_t size)
  {
    maxHistorySize = size;
    if (maxHistorySize == 0) {
      undoStack.clear();
      redoStack.clear();
      return;
    }

    if (undoStack.size() > maxHistorySize) {
      undoStack.erase(undoStack.begin(), undoStack.end() - maxHistorySize);
    }

    if (redoStack.size() > maxHistorySize) {
      redoStack.erase(redoStack.begin(), redoStack.end() - maxHistorySize);
    }
  }

  uint64_t History::getMemoryUsage()
  {
    uint64_t total = 0;
    for(auto &entry : redoStack) {
      total += entry->getMemoryUsage();
    }
    for(auto &entry : undoStack) {
      total += entry->getMemoryUsage();
    }
    return total;
  }

  History& getHistory()
  {
    if (!activeStack.empty()) {
      return *activeStack.back();
    }
    return mainHistory;
  }

  History& getMainHistory()
  {
    return mainHistory;
  }

  EditScope::EditScope(History& h, Project::Scene& scene, Project::Selection& selection)
    : history(h),
      prevScene(h.getBoundScene()),
      prevSelection(h.getBoundSelection())
  {
    history.bind(&scene, &selection);
    activeStack.push_back(&history);
    history.begin();
  }

  EditScope::~EditScope()
  {
    history.end();
    if (!activeStack.empty() && activeStack.back() == &history) {
      activeStack.pop_back();
    }
    history.bind(prevScene, prevSelection);
  }
}
