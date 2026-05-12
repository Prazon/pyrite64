/**
* Weak default for P64::saveAutoInit(). Projects with a generated
* <project>/src/p64/saveTable.cpp provide a strong override that calls
* Game::Save::init(). Builds without a saveTable (engine tests/examples)
* fall through to this no-op.
*/
#include "save/saveAutoInit.h"

namespace P64
{
  __attribute__((weak)) void saveAutoInit() {}
}
