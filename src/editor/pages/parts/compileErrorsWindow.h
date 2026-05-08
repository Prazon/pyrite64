#pragma once

#include <cstdint>

namespace Editor
{
  // Sibling tab to the Log window. Renders structured node-graph compile
  // errors collected during the last build, with double-click navigation
  // back to the offending node. Stateless across runs — backed entirely
  // by ctx.compileErrors.
  class CompileErrorsWindow
  {
   public:
    void draw();

    // Last revision the panel reacted to. EditorScene compares this against
    // ctx.compileErrors.getRevision() to decide whether to auto-pop the tab
    // to the front when new errors appear.
    uint32_t lastSeenRevision{0};
  };
}
