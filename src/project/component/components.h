/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <SDL3/SDL_gpu.h>

#include "imgui.h"
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
  class Viewport2D;
}

struct ImDrawList;
struct SDL_GPUCommandBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPURenderPass;

namespace Project { class Object; }
namespace Renderer { struct UniformGlobal; }

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

  // Called from Viewport2D to paint a screen-space preview of this
  // component's runtime appearance. originScreen is where (obj.pos.x,
  // obj.pos.y) lands in window coords after the 2D viewport's pan/zoom
  // and the Object's anchor offset have been applied. Components write
  // their bounding rect into outMin/outMax (in window coords) so the
  // viewport can do hover-picking and selection outlines uniformly.
  // Optional — components without a 2D preview leave this null and
  // are simply skipped during the 2D walk.
  typedef void(*FuncCompDraw2D)(
    Object&, Entry &entry,
    ImDrawList *drawList,
    ImVec2 originScreen,
    float zoom,
    ImVec2 *outMin, ImVec2 *outMax
  );

  // Intrinsic 2D size used by HBox/VBox layout containers. Returns the
  // component's width/height in canvas pixels. Mirrors the engine-side
  // widgetSize() dispatch in n64/engine/include/scene/widgetSize.h so the
  // canvas preview lays children out exactly the way the runtime will.
  // Optional - non-sized components leave this null and contribute (0,0).
  typedef void(*FuncCompWidgetSize)(
    Object&, Entry &entry,
    int *outW, int *outH
  );

  // Identifies which scene-config layer table a component's draw-layer
  // index points into. Components carrying a per-instance layer index
  // declare their family so layerInspector can patch references when the
  // user adds, removes, duplicates, or resets layers.
  enum class LayerFamily : int { Layer3D = 0, LayerPtx = 1, Layer2D = 2 };

  // Remap a component's stored layer index (and any per-Object override of
  // it) when the scene's layer table changes. `family` is the table that
  // changed; components ignore calls for families they don't reference.
  // `remap` is the index transform: identity for unaffected indices, a
  // shift for insert/delete, or a clamp for reset.
  typedef void(*FuncCompRemapLayer)(
    Object &obj, Entry &entry,
    LayerFamily family,
    const std::function<int(int)> &remap
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
    FuncCompDraw2D      funcDraw2D{};
    FuncCompWidgetSize  funcWidgetSize{};
    FuncCompRemapLayer  funcRemapLayer{};
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
      void draw2D(Object&, Entry &entry, ImDrawList *drawList, \
        ImVec2 originScreen, float zoom, ImVec2 *outMin, ImVec2 *outMax); \
      void widgetSize(Object&, Entry &entry, int *outW, int *outH); \
      void remapLayer(Object&, Entry &entry, LayerFamily family, \
        const std::function<int(int)> &remap); \
    }

  MAKE_COMP(Code)
  MAKE_COMP(Model)
  MAKE_COMP(Light)
  MAKE_COMP(Camera)

  // Picture-in-Picture preview reads runtime projection params
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

    void applyToGlobalUniforms(Object& obj, Entry &entry, Renderer::UniformGlobal &uniGlobal, float screenWidth, float screenHeight);
    float getAspectRatio(Object& obj, Entry &entry, float fallbackAspect);
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
  MAKE_COMP(PaperSprite)
  MAKE_COMP(Path)
  MAKE_COMP(Primitive)
  MAKE_COMP(Sprite2D)
  MAKE_COMP(Label2D)
  MAKE_COMP(ProgressBar2D)
  MAKE_COMP(Panel2D)
  MAKE_COMP(NinePatch2D)
  MAKE_COMP(HBoxLayout)
  MAKE_COMP(VBoxLayout)
  MAKE_COMP(Button2D)

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
      .funcGetAABB = Model::getAABB,
      .funcRemapLayer = Model::remapLayer,
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
      .funcGetAABB = AnimModel::getAABB,
      .funcRemapLayer = AnimModel::remapLayer,
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
      .funcRemapLayer = SpriteBillboard::remapLayer,
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
      .funcRemapLayer = Primitive::remapLayer,
    },
    CompInfo{
      .id = 14,
      .icon = ICON_MDI_IMAGE_OUTLINE " ",
      .name = "Sprite (2D)",
      .funcInit = Sprite2D::init,
      .funcDraw = Sprite2D::draw,
      .funcSerialize = Sprite2D::serialize,
      .funcDeserialize = Sprite2D::deserialize,
      .funcBuild = Sprite2D::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = Sprite2D::draw2D,
      .funcWidgetSize = Sprite2D::widgetSize,
    },
    CompInfo{
      .id = 15,
      .icon = ICON_MDI_FORMAT_TEXT " ",
      .name = "Label (2D)",
      .funcInit = Label2D::init,
      .funcDraw = Label2D::draw,
      .funcSerialize = Label2D::serialize,
      .funcDeserialize = Label2D::deserialize,
      .funcBuild = Label2D::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = Label2D::draw2D,
    },
    CompInfo{
      .id = 16,
      .icon = ICON_MDI_PROGRESS_HELPER " ",
      .name = "Progress Bar (2D)",
      .funcInit = ProgressBar2D::init,
      .funcDraw = ProgressBar2D::draw,
      .funcSerialize = ProgressBar2D::serialize,
      .funcDeserialize = ProgressBar2D::deserialize,
      .funcBuild = ProgressBar2D::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = ProgressBar2D::draw2D,
      .funcWidgetSize = ProgressBar2D::widgetSize,
    },
    CompInfo{
      .id = 17,
      .icon = ICON_MDI_STICKER_OUTLINE " ",
      .name = "Paper Sprite",
      .funcInit = PaperSprite::init,
      .funcDraw = PaperSprite::draw,
      .funcDraw3D = PaperSprite::draw3D,
      .funcSerialize = PaperSprite::serialize,
      .funcDeserialize = PaperSprite::deserialize,
      .funcBuild = PaperSprite::build,
      .funcGetAABB = nullptr,
      .funcRemapLayer = PaperSprite::remapLayer,
    },
    CompInfo{
      .id = 18,
      .icon = ICON_MDI_VECTOR_POLYLINE " ",
      .name = "Path",
      .funcInit = Path::init,
      .funcDraw = Path::draw,
      .funcDraw3D = Path::draw3D,
      .funcSerialize = Path::serialize,
      .funcDeserialize = Path::deserialize,
      .funcBuild = Path::build,
      .funcGetAABB = nullptr,
      .funcDrawOverlay = Path::drawOverlay,
    },
    CompInfo{
      .id = 19,
      .icon = ICON_MDI_RECTANGLE_OUTLINE " ",
      .name = "Panel (2D)",
      .funcInit = Panel2D::init,
      .funcDraw = Panel2D::draw,
      .funcSerialize = Panel2D::serialize,
      .funcDeserialize = Panel2D::deserialize,
      .funcBuild = Panel2D::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = Panel2D::draw2D,
      .funcWidgetSize = Panel2D::widgetSize,
    },
    CompInfo{
      .id = 20,
      .icon = ICON_MDI_BORDER_ALL_VARIANT " ",
      .name = "Nine Patch (2D)",
      .funcInit = NinePatch2D::init,
      .funcDraw = NinePatch2D::draw,
      .funcSerialize = NinePatch2D::serialize,
      .funcDeserialize = NinePatch2D::deserialize,
      .funcBuild = NinePatch2D::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = NinePatch2D::draw2D,
      .funcWidgetSize = NinePatch2D::widgetSize,
    },
    CompInfo{
      .id = 21,
      .icon = ICON_MDI_VIEW_COLUMN " ",
      .name = "HBox Layout",
      .funcInit = HBoxLayout::init,
      .funcDraw = HBoxLayout::draw,
      .funcSerialize = HBoxLayout::serialize,
      .funcDeserialize = HBoxLayout::deserialize,
      .funcBuild = HBoxLayout::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = HBoxLayout::draw2D,
    },
    CompInfo{
      .id = 22,
      .icon = ICON_MDI_VIEW_AGENDA " ",
      .name = "VBox Layout",
      .funcInit = VBoxLayout::init,
      .funcDraw = VBoxLayout::draw,
      .funcSerialize = VBoxLayout::serialize,
      .funcDeserialize = VBoxLayout::deserialize,
      .funcBuild = VBoxLayout::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = VBoxLayout::draw2D,
    },
    CompInfo{
      .id = 23,
      .icon = ICON_MDI_GESTURE_TAP " ",
      .name = "Button (2D)",
      .funcInit = Button2D::init,
      .funcDraw = Button2D::draw,
      .funcSerialize = Button2D::serialize,
      .funcDeserialize = Button2D::deserialize,
      .funcBuild = Button2D::build,
      .funcGetAABB = nullptr,
      .funcDraw2D = Button2D::draw2D,
      .funcWidgetSize = Button2D::widgetSize,
    },
  };

  extern std::array<CompInfo, TABLE.size()> TABLE_SORTED_BY_NAME;

  void init();
}
