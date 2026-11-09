#pragma once
namespace Wheel {
bool IsOpen();
void Tick();              // key handling, time scale (game tick)
void Render(void* dev);   // draw the wheel (ImGui frame)
void ReleaseTextures();
}
