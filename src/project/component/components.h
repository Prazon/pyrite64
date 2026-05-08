/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <array>
#include <memory>
#include <SDL3/SDL_gpu.h>

#include "json.hpp"
#include "IconsMaterialDesignIcons.h"
#include "../../build/sceneContext.h"
#include "../../utils/aabb.h"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"
#include "glm/gtc/quaternion.hpp"

namespace Editor
{
  class Viewport3D;
}

struct ImDrawList;
struct SDL_GPUCommandBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPURenderPass;

namespace Project { class Object; }

namespace Project::Component
{
  struct Entry
  {
    int id{};
    uint64_t uuid{};
    std::string name{};
    std::shared_ptr<void> data{};
  };

  typedef void(*FuncCompDraw)(Object&, Entry &entry);
  typedef void(*FuncCompDraw3D)(Object&, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer* cmdBuff, SDL_GPURenderPass* pass);
  typedef void(*FuncCompCopyPass)(Object&, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass* pass);
  typedef std::shared_ptr<void>(*FuncCompInit)(Object&);
  typedef nlohmann::json(*FuncCompSerial)(const Entry &entry);
  typedef std::shared_ptr<void>(*FuncCompDeserial)(nlohmann::json &doc);
  typedef void(*FuncCompBuild)(Object&, Entry &entry, Build::SceneCtx &ctx);
  typedef Utils::AABB(*FuncCompGetAABB)(Object&, Entry &entry);

  // Called after the 3D framebuffer is composited into the viewport's ImGui
  // window. Lets components draw screen-space ImGui overlays (textured
  // billboard previews, anchor markers, etc.) using the current camera
  // matrices. Optional — leave nullptr if not used.
  typedef void(*FuncCompDrawOverlay)(
    Object&, Entry &entry,
    Editor::Viewport3D &vp,
    ImDrawList *drawList,
    const glm::mat4 &cameraMat, const glm::mat4 &projMat,
    const glm::vec4 &viewportRect /* {x, y, w, h} in window coords */
  );

  struct CompInfo
  {
    int id{};
    int prio{};
    const char* icon{};
    const char* name{};
    FuncCompInit funcInit{};
    FuncCompDraw funcUpdate{};
    FuncCompDraw funcDraw{};
    FuncCompDraw3D funcDraw3D{};
    FuncCompDraw3D funcDrawPost3D{};
    FuncCompCopyPass funcDrawCopyPass{};
    FuncCompSerial funcSerialize{};
    FuncCompDeserial funcDeserialize{};
    FuncCompBuild funcBuild{};
    FuncCompGetAABB funcGetAABB{};
    FuncCompDrawOverlay funcDrawOverlay{};
  };

  #define MAKE_COMP(name) \
    namespace name \
    { \
      std::shared_ptr<void> init(Object& obj); \
      void update(Object& obj, Entry &entry); \
      void draw(Object& obj, Entry &entry); \
      void draw3D(Object&, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer* cmdBuff, SDL_GPURenderPass* pass); \
      void drawCopyPass(Object&, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer* cmdBuff, SDL_GPUCopyPass* pass); \
      nlohmann::json serialize(const Entry &entry); \
      std::shared_ptr<void> deserialize(nlohmann::json &doc); \
      void build(Object&, Entry &entry, Build::SceneCtx &ctx); \
      Utils::AABB getAABB(Object &obj, Entry &entry); \
      void drawOverlay(Object&, Entry &entry, Editor::Viewport3D &vp, \
        ImDrawList *drawList, const glm::mat4 &cameraMat, const glm::mat4 &projMat, \
        const glm::vec4 &viewportRect); \
    }

  MAKE_COMP(Code)
  MAKE_COMP(Model)
  MAKE_COMP(Light)
  MAKE_COMP(Camera)

  // SPBF64 fork: Picture-in-Picture preview reads runtime projection params
  // out of a selected Camera component to render the scene through it.
  namespace Camera
  {
    struct Spec
    {
      glm::vec3 pos{};
      glm::quat rot{0,0,0,1};
      float fov{65.0f};       // degrees
      float nearD{100.0f};
      float farD{4000.0f};
      float aspect{0.0f};     // 0 means "derive from vpSize"
      glm::ivec2 vpSize{320, 240};
    };
    Spec extractSpec(Object &obj, Entry &entry);
  }

  MAKE_COMP(CollMesh)
  MAKE_COMP(CollBody)
  MAKE_COMP(RigidBody)
  MAKE_COMP(Audio2D)
  MAKE_COMP(Constraint)
  MAKE_COMP(Culling)
  MAKE_COMP(NodeGraph)
  MAKE_COMP(AnimModel)
  MAKE_COMP(SpriteBillboard)
  MAKE_COMP(Primitive)

  constexpr std::array TABLE{
    CompInfo{
      .id = 0,
      .icon = ICON_MDI_SCRIPT " ",
      .name = "Code",
      .funcInit = Code::init,
      .funcDraw = Code::draw,
      .funcSerialize = Code::serialize,
      .funcDeserialize = Code::deserialize,
      .funcBuild = Code::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 1,
      .icon = ICON_MDI_CUBE_OUTLINE " ",
      .name = "Model (Static)",
      .funcInit = Model::init,
      .funcDraw = Model::draw,
      .funcDraw3D = Model::draw3D,
      .funcSerialize = Model::serialize,
      .funcDeserialize = Model::deserialize,
      .funcBuild = Model::build,
      .funcGetAABB = Model::getAABB
    },
    CompInfo{
      .id = 2,
      .icon = ICON_MDI_LIGHTBULB_ON_OUTLINE " ",
      .name = "Light",
      .funcInit = Light::init,
      .funcUpdate = Light::update,
      .funcDraw = Light::draw,
      .funcDraw3D = Light::draw3D,
      .funcSerialize = Light::serialize,
      .funcDeserialize = Light::deserialize,
      .funcBuild = Light::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 3,
      .icon = ICON_MDI_VIDEO_VINTAGE " ",
      .name = "Camera",
      .funcInit = Camera::init,
      .funcUpdate = Camera::update,
      .funcDraw = Camera::draw,
      .funcDraw3D = Camera::draw3D,
      .funcSerialize = Camera::serialize,
      .funcDeserialize = Camera::deserialize,
      .funcBuild = Camera::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 4,
      .icon = ICON_MDI_LANDSLIDE_OUTLINE " ",
      .name = "Collision-Mesh",
      .funcInit = CollMesh::init,
      .funcDraw = CollMesh::draw,
      .funcDrawPost3D = CollMesh::draw3D,
      .funcSerialize = CollMesh::serialize,
      .funcDeserialize = CollMesh::deserialize,
      .funcBuild = CollMesh::build,
      .funcGetAABB = CollMesh::getAABB
    },
    CompInfo{
      .id = 5,
      .icon = ICON_MDI_CYLINDER " ",
      .name = "Collider",
      .funcInit = CollBody::init,
      .funcDraw = CollBody::draw,
      .funcDrawPost3D = CollBody::draw3D,
      .funcSerialize = CollBody::serialize,
      .funcDeserialize = CollBody::deserialize,
      .funcBuild = CollBody::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 6,
      .icon = ICON_MDI_MUSIC " ",
      .name = "Audio (2D)",
      .funcInit = Audio2D::init,
      .funcDraw = Audio2D::draw,
      .funcDrawPost3D = Audio2D::draw3D,
      .funcSerialize = Audio2D::serialize,
      .funcDeserialize = Audio2D::deserialize,
      .funcBuild = Audio2D::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 7,
      .prio = -2, // constraint must come before culling and any drawing
      .icon = ICON_MDI_LINK " ",
      .name = "Constraint",
      .funcInit = Constraint::init,
      .funcDraw = Constraint::draw,
      .funcDrawPost3D = Constraint::draw3D,
      .funcSerialize = Constraint::serialize,
      .funcDeserialize = Constraint::deserialize,
      .funcBuild = Constraint::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 8,
      .prio = -1, // culling must come before any models
      .icon = ICON_MDI_EYE_OFF_OUTLINE " ",
      .name = "Culling",
      .funcInit = Culling::init,
      .funcDraw = Culling::draw,
      .funcDrawPost3D = Culling::draw3D,
      .funcSerialize = Culling::serialize,
      .funcDeserialize = Culling::deserialize,
      .funcBuild = Culling::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 9,
      .icon = ICON_MDI_GRAPH_OUTLINE " ",
      .name = "Node Graph",
      .funcInit = NodeGraph::init,
      .funcDraw = NodeGraph::draw,
      .funcDraw3D = NodeGraph::draw3D,
      .funcSerialize = NodeGraph::serialize,
      .funcDeserialize = NodeGraph::deserialize,
      .funcBuild = NodeGraph::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 10,
      .icon = ICON_MDI_HUMAN " ",
      .name = "Model (Animated)",
      .funcInit = AnimModel::init,
      .funcDraw = AnimModel::draw,
      .funcDraw3D = AnimModel::draw3D,
      .funcDrawCopyPass = AnimModel::drawCopyPass,
      .funcSerialize = AnimModel::serialize,
      .funcDeserialize = AnimModel::deserialize,
      .funcBuild = AnimModel::build,
      .funcGetAABB = AnimModel::getAABB
    },
    CompInfo{
      .id = 11,
      .icon = ICON_MDI_CYLINDER " ",
      .name = "Rigid-Body",
      .funcInit = RigidBody::init,
      .funcDraw = RigidBody::draw,
      .funcDrawPost3D = RigidBody::draw3D,
      .funcSerialize = RigidBody::serialize,
      .funcDeserialize = RigidBody::deserialize,
      .funcBuild = RigidBody::build,
      .funcGetAABB = nullptr
    },
    CompInfo{
      .id = 12,
      .icon = ICON_MDI_IMAGE_OUTLINE " ",
      .name = "Sprite Billboard",
      .funcInit = SpriteBillboard::init,
      .funcDraw = SpriteBillboard::draw,
      .funcDraw3D = SpriteBillboard::draw3D,
      .funcSerialize = SpriteBillboard::serialize,
      .funcDeserialize = SpriteBillboard::deserialize,
      .funcBuild = SpriteBillboard::build,
      .funcGetAABB = nullptr,
      // funcDrawOverlay intentionally null: SpriteBillboard now renders as a
      // textured 3D quad via the billboard pipeline in draw3D, not as an
      // ImGui screen-space overlay.
    },
    CompInfo{
      .id = 13,
      .icon = ICON_MDI_SHAPE_OUTLINE " ",
      .name = "Primitive",
      .funcInit = Primitive::init,
      .funcDraw = Primitive::draw,
      .funcDrawPost3D = Primitive::draw3D,
      .funcSerialize = Primitive::serialize,
      .funcDeserialize = Primitive::deserialize,
      .funcBuild = Primitive::build,
      .funcGetAABB = nullptr,
    },
  };

  extern std::array<CompInfo, TABLE.size()> TABLE_SORTED_BY_NAME;

  void init();
}
