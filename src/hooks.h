#pragma once

namespace Hooks {
bool InstallRenderHooks();   // IDirect3DDevice9::Present / Reset
bool InstallInputHooks();    // DirectInput + cursor, so the game ignores input while the menu is open
bool InstallGameHooks();     // CGame::Process tick (v1.0 US only)
}
