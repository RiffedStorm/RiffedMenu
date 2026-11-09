#pragma once
namespace Combat {
void  Tick();                       // scan peds, drive the aimbot, count kills (game tick)
void  Render();                     // wallhack and FOV circle (ImGui frame)
bool  InstallBulletHook();          // exploding bullets
void  SpawnPendingExplosions();
int   Kills();                      // this session
}
