/**
* @copyright 2026 - Prazon
* @license MIT
*/
#pragma once
#include <string>
#include <vector>

namespace Build
{
  // Lightweight enumerator: returns the mesh-node names from a glTF/glb,
  // without parsing buffers, materials, or geometry. Used as a fallback
  // when T3DM::parseGLTF refuses an asset (no materials at all, or no
  // fast64 extras data) so the editor can still expose mesh nodes for
  // collision-mesh assignment.
  //
  // Skips the "fast64_f3d_material_library" sentinel node to match the
  // upstream importer's behavior.
  std::vector<std::string> enumerateGltfMeshNodes(const std::string &gltfPath);
}
