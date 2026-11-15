#pragma once

// Frosted-glass backdrop: a blurred, quarter-size copy of the game frame for the menu panels.
// The menu asks for it while it draws; the overlay renderer then captures and blurs the back
// buffer right before ImGui renders, so the panels show this frame's game image.
namespace Blur {

void  Request();               // the menu wants a backdrop this frame
void* Texture();               // IDirect3DTexture9* to draw (screen-space UVs), or nullptr: use a plain tint
void  Capture(void* device);   // call outside BeginScene/EndScene, before rendering ImGui
void  Release();               // before IDirect3DDevice9::Reset and on shutdown

}
