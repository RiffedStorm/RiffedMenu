#pragma once
namespace Wheel {
bool IsOpen();
void Tick();              // key handling, time scale (game tick)
void Render(void* dev);   // draw the wheel (ImGui frame)
void ReleaseTextures();
void InstallSaveHooks();  // keeps the collected guns with each save slot
void ForgetCollected();   // the player no longer owns anything
void PlayerGone();        // no player right now: a game is being torn down or loaded
}
