#pragma once
#include <functional>

// Trainer state shared between the menu (render thread) and the game tick
// (Present hook, main thread). Both run on the main thread, but
// actions are still queued so world changes never happen mid-render.

struct Toggles {
    bool godMode        = false;
    bool infiniteArmour = false;
    bool infiniteAmmo   = false;
    bool neverWanted    = false;
    bool noclip         = false;
    float noclipSpeed   = 20.0f;   // m/s, Shift = x4
    bool vehicleGod     = false;
    bool autoRepair     = false;
    bool freezeTime     = false;
    int  frozenHour     = 12;
    int  frozenMinute   = 0;
    bool lockWeather    = false;
    int  weather        = 0;
    bool spawnInside    = true;
    char plate[9]       = "";
    float damageMult    = 1.0f;
};

// Orbiting preview camera for the editors. The menu fills this in every frame it wants a
// preview; the tick applies it (and restores the normal camera once nobody asks for it).
struct PreviewCam {
    bool  active = false;
    bool  onVehicle = false;
    float yaw = 0.0f, dist = 6.0f, height = 1.5f;
};

extern Toggles g_toggles;
extern bool    g_gameFocused;
extern PreviewCam g_preview;   // game window focused and menu closed (set by the Present hook)

namespace Features {
void Queue(std::function<void()> fn);   // run on the next game tick
void Tick();                            // called once per frame from the Present hook
}
