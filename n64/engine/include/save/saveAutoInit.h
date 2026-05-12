/**
* P64::saveAutoInit() — engine-side hook called from main() before
* the GAME_INIT global-script hook fires. Intended for projects that
* author .p64save schema assets: the project's generated saveTable.cpp
* provides a strong override that calls Game::Save::init(), wiring up
* P64::Save with the right slot count.
*
* Engine ships a __weak__ no-op default so test/example ROMs without a
* saveTable still link.
*/
#pragma once

namespace P64
{
  void saveAutoInit();
}
