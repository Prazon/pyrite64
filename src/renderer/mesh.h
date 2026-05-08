/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <memory>
#include <SDL3/SDL.h>

#include "vertBuffer.h"
#include "vertex.h"
#include "../utils/aabb.h"
#include "glm/vec3.hpp"

namespace Renderer
{
  class Scene;

  // enable_shared_from_this so recreate()'s deferred copy-pass lambda can
  // hold the mesh alive until it runs. Without this, when a Viewport3D
  // destroys its per-frame meshes the lambda is left holding a dangling
  // `this` and blows up on the next frame's copy-pass drain.
  class Mesh : public std::enable_shared_from_this<Mesh>
  {
    private:
      Renderer::VertBuffer *vertBuff{nullptr};
      bool dataReady = false;
      Utils::AABB aabb{};

    public:
      std::vector<Renderer::Vertex> vertices{};
      std::vector<Renderer::LineVertex> vertLines{};
      std::vector<uint16_t> indices{};

      void recreate(Renderer::Scene &scene, bool clearData = true);

      void draw(SDL_GPURenderPass* pass, uint32_t indexOffset = 0, uint32_t indexCount = 0);

      const Utils::AABB& getAABB() const { return aabb; }

      Mesh();
      ~Mesh();
  };
}