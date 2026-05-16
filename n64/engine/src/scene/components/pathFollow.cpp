/**
* PathFollow implementation.
* Each update advances an arc-length cursor along the resolved Path and
* writes the owning object's transform from the sampled PathFrame. The
* frame is in the Path object's local space, so it is lifted into world
* space through that object before being applied to the follower.
*/
#include "scene/components/pathFollow.h"
#include "scene/components/path.h"
#include "scene/path.h"
#include "scene/scene.h"
#include "lib/math.h"
#include <t3d/t3dmath.h>

namespace
{
  struct __attribute__((packed)) InitData
  {
    uint16_t refObjId;
    uint8_t  mode;
    uint8_t  flags;
    float    speed;
    float    startDist;
  };
  static_assert(sizeof(InitData) == 12);

  // Resolve the Path to follow + the object that owns it (whose transform
  // lifts the local-space frame into world space). Order: this object, then
  // parent, then the explicit reference object.
  P64::Comp::Path* resolvePath(P64::Object& obj, uint16_t refObjId,
                               P64::Object*& outPathObj)
  {
    if (auto* p = obj.getComponent<P64::Comp::Path>()) {
      outPathObj = &obj;
      return p;
    }
    if (P64::Object* par = obj.getParent()) {
      if (auto* p = par->getComponent<P64::Comp::Path>()) {
        outPathObj = par;
        return p;
      }
    }
    if (refObjId != 0) {
      P64::Object* ref = obj.getScene().getObjectById(refObjId);
      if (ref) {
        if (auto* p = ref->getComponent<P64::Comp::Path>()) {
          outPathObj = ref;
          return p;
        }
      }
    }
    return nullptr;
  }
}

namespace P64::Comp
{
  void PathFollow::initDelete(Object& obj, PathFollow* data, void* initData_)
  {
    (void)obj;
    auto initData = (InitData*)initData_;
    if (initData == nullptr) {
      data->~PathFollow();
      return;
    }

    new(data) PathFollow();
    data->refObjId = initData->refObjId;
    data->mode     = initData->mode;
    data->flags    = initData->flags;
    data->speed    = initData->speed;
    data->distance = initData->startDist;
    data->dir      = 1;
    data->playing  = (initData->flags & FLAG_AUTOPLAY) ? 1 : 0;
  }

  void PathFollow::update(Object& obj, PathFollow* data, float deltaTime)
  {
    if (!data->playing) return;

    Object* pathObj = nullptr;
    Path* path = resolvePath(obj, data->refObjId, pathObj);
    if (!path || !pathObj) return;

    float len = Path::length(*path);
    if (len <= 0.0f) return;

    data->distance += data->speed * (float)data->dir * deltaTime;

    switch (data->mode) {
      case MODE_LOOP:
        // fmodf keeps sign, so fold negatives back into [0, len).
        data->distance = fmodf(data->distance, len);
        if (data->distance < 0.0f) data->distance += len;
        break;

      case MODE_PINGPONG:
        if (data->distance > len) {
          data->distance = len - (data->distance - len);
          data->dir = (int8_t)-data->dir;
        } else if (data->distance < 0.0f) {
          data->distance = -data->distance;
          data->dir = (int8_t)-data->dir;
        }
        break;

      default: // MODE_ONCE
        if (data->distance >= len) {
          data->distance = len;
          data->playing = 0; // reached the end, hold here
        } else if (data->distance < 0.0f) {
          data->distance = 0.0f;
          data->playing = 0;
        }
        break;
    }

    PathFrame frame = Path::sample(*path, data->distance);

    // Position: lift the local-space sample through the Path object.
    obj.pos = pathObj->outOfLocalSpace(frame.pos);

    if (data->flags & FLAG_ORIENT) {
      // Rotate the frame basis into world space by the Path object's
      // rotation, then derive the object rotation from a look-at view
      // matrix (same path the Camera/Constraint components use).
      fm_vec3_t wFwd = pathObj->rot * frame.fwd;
      fm_vec3_t wUp  = pathObj->rot * frame.up;
      fm_vec3_norm(&wFwd, &wFwd);
      fm_vec3_norm(&wUp, &wUp);

      fm_vec3_t eye    = obj.pos;
      fm_vec3_t target = eye + wFwd;
      fm_mat4_t view;
      t3d_mat4_look_at(&view, &eye, &target, &wUp);
      obj.rot = Math::quatFromInvRotMat(view);
    }
  }
}
