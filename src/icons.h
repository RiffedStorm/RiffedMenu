#pragma once
#include "imgui.h"

// Line icons drawn with the draw list, so they stay sharp at every UI scale.
namespace Icons {

enum Id {
    Player, Vehicle, Customize, Weapons, Gameplay, Combat, Missions, Outfit, Teleport, World, Stats, Settings,
    Search, Close, Check, ChevronDown, Bell, Keyboard,
};

// size: the icon's box edge in pixels, centred on c.
void Draw(ImDrawList* dl, Id icon, ImVec2 c, float size, ImU32 col);

}
