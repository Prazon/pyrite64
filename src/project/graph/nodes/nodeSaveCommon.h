/**
* @copyright 2026 - Prazon
* @license MIT
*
* Shared helpers for the SaveGet/SaveSet/Save{Commit,Reload,ClearAll} nodes.
* Each Get/Set node persists a {groupUUID, fieldName} pair and resolves it
* against the live AssetManager to render a dropdown in the inspector and
* emit the matching Game::Save::<Group>::<get|set><Field> call at build.
*/
#pragma once

#include <string>
#include "baseNode.h"
#include "../../../context.h"
#include "../../../project/project.h"
#include "../../../project/assetManager.h"
#include "../../../project/assets/saveFileAsset.h"
#include "imgui.h"

namespace Project::Graph::Node::SaveHelpers
{
  using SaveAsset = ::Project::Assets::SaveFileAsset;

  // Sanitize raw asset name → C++ identifier (matches saveTableBuilder).
  inline std::string sanitizeIdent(const std::string &raw)
  {
    std::string g;
    g.reserve(raw.size());
    for (char c : raw) {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
          || (c >= '0' && c <= '9') || c == '_') {
        g.push_back(c);
      } else {
        g.push_back('_');
      }
    }
    if (!g.empty() && g[0] >= '0' && g[0] <= '9') g.insert(g.begin(), '_');
    return g;
  }

  inline std::string upper1(const std::string &s)
  {
    if (s.empty()) return s;
    std::string r = s;
    if (r[0] >= 'a' && r[0] <= 'z') r[0] = (char)(r[0] - 'a' + 'A');
    return r;
  }

  // Looks up the SAVE_FILE asset entry for the given group uuid. Returns
  // nullptr if not found (asset was deleted or graph hasn't been migrated).
  inline const SaveAsset *findGroup(uint64_t groupUUID)
  {
    if (!ctx.project) return nullptr;
    auto *e = ctx.project->getAssets().getEntryByUUID(groupUUID);
    if (!e || e->type != ::Project::FileType::SAVE_FILE || !e->saveFileAsset) {
      return nullptr;
    }
    return e->saveFileAsset.get();
  }

  // Locate a field by name within a group.
  inline const SaveAsset::Field *findField(const SaveAsset *grp, const std::string &fieldName)
  {
    if (!grp) return nullptr;
    for (const auto &f : grp->fields) {
      if (f.name == fieldName) return &f;
    }
    return nullptr;
  }

  // Resolve to a fully-qualified C++ identifier suffix:
  // "<groupNs>::<get|set><Field>". Returns empty string if either piece is
  // missing (graph node should fall back to an error stripe in that case).
  inline std::string resolveCall(uint64_t groupUUID, const std::string &fieldName,
                                 const std::string &accessorPrefix /* "get" or "set" */)
  {
    auto *g = findGroup(groupUUID);
    if (!g) return {};
    auto *f = findField(g, fieldName);
    if (!f) return {};
    std::string ns = sanitizeIdent(g->groupName);
    if (ns.empty()) return {};
    std::string fid = sanitizeIdent(f->name);
    if (fid.empty()) return {};
    return ns + "::" + accessorPrefix + upper1(fid);
  }

  // Renders a Group + Field dropdown pair. Optionally restricts the field
  // list by type (used by the typed Get/Set nodes so an Int Set node only
  // lists int fields). If `filterType` is negative, all field types pass.
  // Returns true when either selection changed.
  inline bool drawSelectors(uint64_t &groupUUID, std::string &fieldName,
                            int filterType /* -1 for any, else SaveAsset::FieldType */)
  {
    bool changed = false;
    if (!ctx.project) {
      ImGui::TextDisabled("(no project)");
      return false;
    }
    const auto &saves = ctx.project->getAssets().getTypeEntries(::Project::FileType::SAVE_FILE);

    // Group dropdown
    std::string groupLabel = "(pick group)";
    const SaveAsset *currentGroup = findGroup(groupUUID);
    if (currentGroup) {
      auto *e = ctx.project->getAssets().getEntryByUUID(groupUUID);
      groupLabel = e ? e->name : groupLabel;
    }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##saveGroup", groupLabel.c_str())) {
      for (const auto &e : saves) {
        if (!e.saveFileAsset) continue;
        bool sel = (e.saveFileAsset->uuid == groupUUID);
        std::string lbl = e.name + "##" + std::to_string(e.saveFileAsset->uuid);
        if (ImGui::Selectable(lbl.c_str(), sel)) {
          if (groupUUID != e.saveFileAsset->uuid) {
            groupUUID  = e.saveFileAsset->uuid;
            fieldName.clear();
            changed = true;
          }
        }
      }
      ImGui::EndCombo();
    }

    // Field dropdown
    ImGui::SameLine();
    std::string fieldLabel = fieldName.empty() ? "(pick field)" : fieldName;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##saveField", fieldLabel.c_str())) {
      auto *g = findGroup(groupUUID);
      if (g) {
        for (const auto &f : g->fields) {
          if (filterType >= 0 && (int)f.type != filterType) continue;
          bool sel = (f.name == fieldName);
          if (ImGui::Selectable(f.name.c_str(), sel)) {
            if (fieldName != f.name) {
              fieldName = f.name;
              changed = true;
            }
          }
        }
      }
      ImGui::EndCombo();
    }
    return changed;
  }
}
