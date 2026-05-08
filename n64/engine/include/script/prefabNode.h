/**
* @copyright 2026 - Prazon
* @license MIT
*
* Prefab event-graph node tagging.
*
* User code under <project>/src/user/<prefabName>.{h,cpp} declares functions
* that the editor exposes as callable nodes in the prefab's event graph. The
* P64_NODE macro is a discovery tag: it expands to nothing at compile time
* but is regex-matched by the editor's scanner to build the node palette.
*
* Example (in src/user/Player.h):
*
*   #include "script/prefabNode.h"
*   #include "p64/prefabVars.h"
*
*   namespace User::Player {
*     P64_NODE void OnReady(P64::Object* self);
*     P64_NODE void OnHurt(P64::Object* self, int amount);
*     P64_NODE int  GetHealth(P64::Object* self);
*   }
*/
#pragma once
#include <libdragon.h>
#include "scene/scene.h"
#include "scene/object.h"

// Empty macro: the body of a P64_NODE-tagged function compiles unchanged.
// The editor's source scanner looks for the literal token to find tagged
// declarations.
#define P64_NODE
