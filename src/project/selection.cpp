// added by SPBF64 fork
#include "selection.h"

#include "scene/scene.h"

void Project::Selection::sanitize(Scene *scene)
{
  if (!scene) {
    clear();
    return;
  }

  auto keepIt = std::remove_if(uuids.begin(), uuids.end(),
    [scene](uint32_t uuid) { return !scene->getObjectByUUID(uuid); }
  );
  if (keepIt != uuids.end()) {
    uuids.erase(keepIt, uuids.end());
  }

  if (!isSelected(primaryUUID)) {
    primaryUUID = uuids.empty() ? 0 : uuids.back();
  }
}
