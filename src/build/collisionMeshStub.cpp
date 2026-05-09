/**
* @copyright 2026 - Prazon
* @license MIT
*/
#include "collisionMeshStub.h"
#include "tiny3d/tools/gltf_importer/src/lib/cgltf.h"

namespace Build
{
  std::vector<std::string> enumerateGltfMeshNodes(const std::string &gltfPath)
  {
    std::vector<std::string> names{};

    cgltf_options options{};
    cgltf_data *data = nullptr;
    if(cgltf_parse_file(&options, gltfPath.c_str(), &data) != cgltf_result_success) {
      return names;
    }
    if(cgltf_validate(data) != cgltf_result_success) {
      cgltf_free(data);
      return names;
    }

    for(cgltf_size i = 0; i < data->nodes_count; ++i) {
      const cgltf_node *node = &data->nodes[i];
      if(!node->mesh) continue;
      if(node->name && std::string(node->name).starts_with("fast64_f3d_material_library")) {
        continue;
      }
      if(node->name && node->name[0]) {
        names.emplace_back(node->name);
      }
    }

    cgltf_free(data);
    return names;
  }
}
