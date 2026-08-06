/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>

/**
 * Object flags used in the flag mask in P64::Object.
 */
namespace P64::ObjectFlags
{
  constexpr uint16_t SELF_ACTIVE        = 1 << 0; // if true, object will be updated this frame
  constexpr uint16_t PARENTS_ACTIVE     = 1 << 1; // true if all parent(s) are active, used to determine final active state
  constexpr uint16_t HAS_CHILDREN       = 1 << 2; // true if object has children (aka other objects list this as their parent ID)
  constexpr uint16_t PENDING_REMOVE     = 1 << 3; // flagged for removal at the end of the frame
  constexpr uint16_t PENDING_ACTIVE_CHG = 1 << 4; // flagged to toggle active state after all updates
  constexpr uint16_t IS_CULLED          = 1 << 5; // if true, object is not drawn this frame (usually set by culling logic)
  constexpr uint16_t SELF_HIDDEN        = 1 << 6; // if true, object drawing is disabled
  constexpr uint16_t PARENTS_HIDDEN     = 1 << 7; // true if all ancestors are hidden, used to determine final hidden state

  // Fork flags live above upstream's range so future upstream bits slot in
  // below without renumbering. Both are regenerated into scene binaries on
  // every build, so moving them is safe as long as editor + engine agree.

  // File-format only: signals that a fixed-size prefab-variable block follows
  // the component terminator. The engine reader skips/loads it depending on
  // whether the prefab actor system is wired up; the runtime never sets this
  // bit on its own (it is purely a serialization marker).
  constexpr uint16_t HAS_PREFAB_VARS    = 1 << 8;

  // Object renders in the screen-space 2D pass (DrawLayer::use2D) instead of
  // the world-space 3D pass. Set on every Object whose ancestor chain includes
  // a Canvas-marked Object — sceneBuilder propagates the flag down at write
  // time so the runtime draw loop just consults this bit per object.
  // Components on a 2D-flagged Object should treat obj.pos.x/y as pixel
  // coordinates (320×240 N64 framebuffer) and obj.pos.z as draw-order depth.
  constexpr uint16_t RENDER_LAYER_2D    = 1 << 9;

  constexpr uint16_t ACTIVE = SELF_ACTIVE | PARENTS_ACTIVE;
  constexpr uint16_t HIDDEN = SELF_HIDDEN | PARENTS_HIDDEN;
}
