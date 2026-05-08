/**
* Label2D component implementation.
* The text string is variable-length so the InitData header carries a
* uint16 length and is followed by N bytes of payload. We copy the payload
* into a malloc'd buffer at init time and free it on destroy.
*/
#include "scene/components/label2D.h"
#include "scene/scene.h"
#include "renderer/drawLayer.h"
#include <malloc.h>
#include <string.h>

namespace
{
  // Header laid out by compLabel2D.cpp::build.  Followed by `textLen` bytes
  // of UTF-8 text (no NUL terminator on disk) — the runtime adds the NUL
  // when copying into the owned buffer.
  struct __attribute__((packed)) InitDataHeader
  {
    uint16_t textLen;
    uint8_t  fontSlot;
    uint8_t  styleId;
    uint8_t  colorR;
    uint8_t  colorG;
    uint8_t  colorB;
    uint8_t  colorA;
  };
  static_assert(sizeof(InitDataHeader) == 8);
}

namespace P64::Comp
{
  void Label2D::initDelete(Object &obj, Label2D* data, void* initData_)
  {
    if (initData_ == nullptr) {
      // destroy: free the owned text copy
      if (data->text) free(data->text);
      data->text = nullptr;
      return;
    }

    auto *hdr = (InitDataHeader*)initData_;
    data->fontSlot = hdr->fontSlot;
    data->styleId  = hdr->styleId;
    data->colorR   = hdr->colorR;
    data->colorG   = hdr->colorG;
    data->colorB   = hdr->colorB;
    data->colorA   = hdr->colorA;

    uint16_t len = hdr->textLen;
    data->text = (char*)malloc((size_t)len + 1);
    if (data->text) {
      if (len > 0) memcpy(data->text, (char*)initData_ + sizeof(InitDataHeader), len);
      data->text[len] = '\0';
    }
    (void)obj;
  }

  void Label2D::draw(Object &obj, Label2D* data, [[maybe_unused]] float deltaTime)
  {
    if (!data->text) return;

    rdpq_set_mode_standard();
    rdpq_set_prim_color({data->colorR, data->colorG, data->colorB, data->colorA});

    rdpq_text_print(NULL, data->fontSlot, (int)obj.pos.x, (int)obj.pos.y, data->text);
  }
}
