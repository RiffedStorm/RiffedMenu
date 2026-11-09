#pragma once
#include <functional>
#include <string>
#include <vector>

// Trainer state shared between the menu and the game tick (both run on the main thread,
// from the Present hook). Every field here is saved by config.cpp.

struct AimbotCfg {
    bool  enabled = false;
    bool  advanced = false;
    int   mode = 0;              // 0 while aiming (RMB), 1 hold key, 2 always when a weapon is aimed
    int   key = 0x05;            // VK_XBUTTON1
    float fov = 25.0f;           // radius in degrees around the crosshair
    float smooth = 0.0f;         // 0 = snap instantly, 0.95 = very slow
    float maxDist = 150.0f;
    int   bone = 0;              // 0 head, 1 chest, 2 pelvis
    int   priority = 0;          // 0 closest to crosshair, 1 nearest, 2 lowest health
    bool  cops = true, enemies = true, attackers = true, missionTargets = true, civilians = false;
    bool  visibleOnly = true;
    bool  sticky = true;
    bool  prediction = false;
    bool  ignoreInVehicles = false;
    bool  drawFov = false;
    bool  driveBy = false;       // also aim during free-look drive-by
};

struct EspCfg {
    bool  enabled = false;
    bool  advanced = false;
    bool  onlyAimbot = false;    // show only peds the aimbot would accept right now
    bool  box = true, health = true, armour = false, distance = false, label = false, snapline = false;
    bool  cops = true, enemies = true, civilians = false, friends = false;
    float maxDist = 150.0f;
    float thickness = 1.5f;
    float colCop[4]    = { 0.30f, 0.60f, 1.00f, 1.00f };
    float colEnemy[4]  = { 1.00f, 0.25f, 0.25f, 1.00f };
    float colCiv[4]    = { 0.85f, 0.85f, 0.85f, 1.00f };
    float colFriend[4] = { 0.30f, 0.95f, 0.40f, 1.00f };
};

struct GameplayCfg {
    bool  customHealth = false;  int maxHealth = 200;
    bool  customArmour = false;  int maxArmour = 150;
    bool  customAmmo = false;    int reserveAmmo = 500;
    float clipMult = 1.0f;
    bool  healthRegen = false;   float healthAmount = 5.0f, healthEvery = 1.0f, healthDelay = 5.0f;
    bool  armourRegen = false;   float armourAmount = 5.0f, armourEvery = 1.0f, armourDelay = 5.0f;
    bool  ammoRegen = false;     int   ammoAmount = 1;      float ammoEvery = 1.0f;
    bool  lifesteal = false;     float healthPerKill = 25.0f, armourPerKill = 0.0f;
    int   moneyPerKill = 0;
    int   damageTaken = 100;     // percent
    bool  explodingBullets = false; int explosionType = 0;
    bool  freeDriveBy = false;
    bool  anyVehicleWeapon = false;
    float driveBySens = 1.0f;    // on top of the game's own mouse sensitivity
    bool  neverBusted = false, noFallOffBike = false, unlimitedStamina = false, infiniteBreath = false;
    bool  fastReload = false, slowmoAiming = false;
    bool  noSpread = false, moveWhileAiming = false, armedSprint = false;
    float slowmoScale = 0.4f;
};

struct NitroCfg { bool infinite = false, noCooldown = false, continuous = false; };

struct MissionCfg { bool preserveStory = true, protectAllies = false; };

struct GpsCfg {
    bool  enabled = true, toObjective = true, onFoot = false;
    float colWaypoint[4] = { 1.00f, 0.13f, 0.13f, 1.00f };
    float colMission[4]  = { 1.00f, 0.85f, 0.10f, 1.00f };
    float width = 3.0f;
};

struct WheelCfg { bool enabled = true, slowmo = true, inVehicles = false; int key = 0x09; float scale = 1.0f; };

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
    float gameSpeed     = 1.0f;
    float gravity       = 0.008f;
    int  ammoMul        = 1;

    int  menuKey   = 0x2D;  // VK_INSERT
    int  noclipKey = 0x76;  // VK_F7

    float textScale = 1.0f;
    float opacity   = 0.96f;
    int   language  = 0;    // 0 English, 1 Polish
    bool  updateNotify = true;

    AimbotCfg   aim;
    EspCfg      esp;
    GameplayCfg play;
    NitroCfg    nitro;
    MissionCfg  mission;
    GpsCfg      gps;
    WheelCfg    wheel;

    std::vector<int> favorites;  // vehicle ids, in the user's order
};

// Orbiting preview camera for the editors. The menu fills this in every frame it wants a
// preview; the tick applies it (and restores the normal camera once nobody asks for it).
struct PreviewCam {
    bool  active = false;
    bool  onVehicle = false;
    float yaw = 0.0f, dist = 6.0f, height = 1.5f;
};

extern Toggles    g_toggles;
extern PreviewCam g_preview;
extern bool       g_gameFocused;   // game window focused and nothing capturing input (set by the Present hook)
extern bool       g_multiplayer;   // SA-MP / MTA detected: combat features stay off

namespace Features {
void Queue(std::function<void()> fn);   // run on the next game tick
void Tick();                            // called once per frame from the Present hook
void NotifyPush(const char* text);      // show a toast from outside the menu
void RefreshBody();                     // redraw CJ after fat/muscle changes, as soon as he is on foot
bool DriveByAiming();                   // the free-look drive-by is running right now

// Crash guard for things that can take the game down. RiskBegin records what is about to be
// applied; if the game dies before RiskEnd, that item is disabled on the next start.
void RiskBegin(const std::string& key);
bool RiskIsBad(const std::string& key);
void RiskClear();                       // forget items disabled by the crash guard
void RiskLoad();
}
