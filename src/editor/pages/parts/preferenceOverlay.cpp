/**
* @copyright 2026 - Nolan Baker
* @license MIT
*/

#include "preferenceOverlay.h"

#include "imgui.h"
#include "../../../context.h"
#include "../../../utils/logger.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"
#include "../../keymap.h"
#include "../../preferences.h"

#include <algorithm>

namespace
{
  // Field defaults come straight from the member initializers on the struct.
  const Editor::Preferences PREF_DEF{};

  void drawNavigation()
  {
    auto &p = ctx.prefs;
    ImTable::start("Navigation");
    ImTable::addPref("Zoom Speed", p.zoomSpeed, PREF_DEF.zoomSpeed,
      "Camera dolly speed when zooming the viewport.");
    ImTable::addPref("WASD Move Speed", p.moveSpeed, PREF_DEF.moveSpeed,
      "Fly-cam translation speed.");
    ImTable::addPref("Modify Move Speed with Wheel", p.mouseWheelModifiesSpeed,
      PREF_DEF.mouseWheelModifiesSpeed,
      "Scroll wheel changes fly-cam speed instead of zooming while navigating.");
    ImTable::addPref("Pan Speed", p.panSpeed, PREF_DEF.panSpeed,
      "Middle-drag pan speed.");
    ImTable::addPref("Look Speed", p.lookSpeed, PREF_DEF.lookSpeed,
      "Mouse-look sensitivity. Negative inverts the Y axis.");
    ImTable::addPref("Invert Wheel Y", p.invertWheelY, PREF_DEF.invertWheelY,
      "Invert mouse-wheel zoom direction.");
    ImTable::end();
  }

  void drawRendering()
  {
    auto &p = ctx.prefs;
    ImTable::start("Rendering");

    // AA is stored as a render-scale factor; expose it as a toggle but keep
    // the default comparison against the factor's default (1.0 = off).
    if (ImTable::prefRow("Anti-Alias", "Supersample the editor viewport (2x). Applies on restart.", true)) {
      bool on = p.renderFactorAA > 1.0f;
      bool def = PREF_DEF.renderFactorAA > 1.0f;
      ImGui::PushID("aa");
      ImGui::PushItemWidth(on != def ? ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeightWithSpacing() : -FLT_MIN);
      if (ImGui::Checkbox("##aa", &on)) p.renderFactorAA = on ? 2.0f : 1.0f;
      ImGui::PopItemWidth();
      if (on != def) {
        ImGui::SameLine(0, 2);
        if (ImGui::Button(ICON_MDI_BACKUP_RESTORE "##aar", ImVec2(-FLT_MIN, 0))) {
          p.renderFactorAA = PREF_DEF.renderFactorAA;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to default.");
      }
      ImGui::PopID();
    }

    if (ctx.forceVSync) {
      p.useVSync = true;
      if (ImTable::prefRow("VSync", "GPU only supports VSync on this system.", false)) {
        ImGui::BeginDisabled();
        bool v = true;
        ImGui::Checkbox("##vs", &v);
        ImGui::EndDisabled();
      }
    } else {
      ImTable::addPref("VSync", p.useVSync, PREF_DEF.useVSync,
        "Sync editor presentation to the display refresh.", true);
    }

    if (!p.useVSync) {
      ImTable::addPref("FPS Limit", p.fpsLimit, PREF_DEF.fpsLimit,
        "Editor frame-rate cap when VSync is off (minimum 20).");
      p.fpsLimit = std::max(20, p.fpsLimit);
    }
    ImTable::end();
  }

  void drawDisplay()
  {
    auto &p = ctx.prefs;
    ImTable::start("Display");
    ImTable::addPref("Rotation as Euler", p.showRotAsEuler, PREF_DEF.showRotAsEuler,
      "Show object rotation as Euler angles instead of a quaternion.");

    int mode = (int)p.contentBrowserMode;
    if (ImTable::addPrefCombo("Content Browser", mode,
          (int)PREF_DEF.contentBrowserMode, { "Unified", "Split" },
          "Layout of the asset browser: a single view or a folders/contents split.")) {
      p.contentBrowserMode = (Editor::ContentBrowserMode)mode;
    }
    ImTable::end();
  }

  void drawKeymap()
  {
    auto &p = ctx.prefs;
    ImTable::start("KeymapPreset");
    int preset = (int)p.keymapPreset;
    if (ImTable::addPrefCombo("Preset", preset, 0, { "Blender", "Industry Compatible" },
          "Base keybinding set. Switching resets all bindings to that preset.")) {
      p.keymapPreset = (Editor::Input::KeymapPreset)preset;
      p.applyKeymapPreset();
    }
    ImTable::end();

    Editor::Input::Keymap d = p.getCurrentKeymapPreset();
    if (ImGui::TreeNodeEx("Global", ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
      ImTable::start("Global");
      ImTable::addKeybind("Save",          p.keymap.save,         d.save);
      ImTable::addKeybind("Copy",          p.keymap.copy,         d.copy);
      ImTable::addKeybind("Paste",         p.keymap.paste,        d.paste);
      ImTable::addKeybind("Reload Assets", p.keymap.reloadAssets, d.reloadAssets);
      ImTable::addKeybind("Build",         p.keymap.build,        d.build);
      ImTable::addKeybind("Build & Run",   p.keymap.buildAndRun,  d.buildAndRun);
      ImTable::addKeybind("Zoom In",       p.keymap.zoomIn,       d.zoomIn);
      ImTable::addKeybind("Zoom Out",      p.keymap.zoomOut,      d.zoomOut);
      ImTable::end();
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("3D View", ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
      ImTable::start("3D View");
      ImTable::addKeybind("Move Forward",    p.keymap.moveForward,    d.moveForward);
      ImTable::addKeybind("Move Back",       p.keymap.moveBack,       d.moveBack);
      ImTable::addKeybind("Move Left",       p.keymap.moveLeft,       d.moveLeft);
      ImTable::addKeybind("Move Right",      p.keymap.moveRight,      d.moveRight);
      ImTable::addKeybind("Move Up",         p.keymap.moveUp,         d.moveUp);
      ImTable::addKeybind("Move Down",       p.keymap.moveDown,       d.moveDown);
      ImTable::addKeybind("Toggle Ortho",    p.keymap.toggleOrtho,    d.toggleOrtho);
      ImTable::addKeybind("Focus Object",    p.keymap.focusObject,    d.focusObject);
      ImTable::addKeybind("Gizmo Translate", p.keymap.gizmoTranslate, d.gizmoTranslate);
      ImTable::addKeybind("Gizmo Rotate",    p.keymap.gizmoRotate,    d.gizmoRotate);
      ImTable::addKeybind("Gizmo Scale",     p.keymap.gizmoScale,     d.gizmoScale);
      ImTable::addKeybind("Delete Object",   p.keymap.deleteObject,   d.deleteObject);
      ImTable::addKeybind("Snap Object",     p.keymap.snapObject,     d.snapObject);
      ImTable::end();
      ImGui::TreePop();
    }
  }
}

void Editor::PreferenceOverlay::draw()
{
  static const std::vector<SettingsCategory> cats = {
    { "navigation", "Navigation", ICON_MDI_GESTURE_TAP, "General", drawNavigation },
    { "rendering",  "Rendering",  ICON_MDI_MONITOR,     "General", drawRendering  },
    { "display",    "Display",    ICON_MDI_EYE,         "General", drawDisplay    },
    { "keymap",     "Keymap",     ICON_MDI_KEYBOARD,    "Input",   drawKeymap     },
  };
  drawSettingsShell("prefs", cats, shellState);
}
